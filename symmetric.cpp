
// Symmetric Team Scatter version of the n-body algorithm

#include <tuple>

// Uncomment for no threading inside P2P
//#define P2P_DECAY_ITERATOR 0
//#define P2P_NUM_THREADS 0

#include "Util.hpp"

#include "kernel/InvSq.kern"
#include "meta/kernel_traits.hpp"
#include "meta/random.hpp"

typedef std::tuple<int,int>      xy_pair;
typedef std::tuple<int,int,int>  itc_tuple;
typedef std::tuple<int,int>      ir_pair;

struct IndexTransformer {
  IndexTransformer(int num_teams, int team_size)
      : T(num_teams), C(team_size) {
  }

  /** Take an (iteration, team, team_rank) tuple
   * and return the (iteration, rank) pair of the transpose block
   */
  ir_pair operator()(int i, int t, int c) const {
    int Y = (t + c + i * C) % T;  //< Column number
    int D = (t - Y + T) % T;      //< Positive distance from diag
    return ir_pair{(D/C), (Y*C) + (D%C)};
  }

 private:
  int T;   // The number of process teams in the computation
  int C;   // The size of the process teams in the computation
};



int main(int argc, char** argv)
{
  bool checkErrors = true;
  unsigned teamsize = 1;

  // Parse optional command line args
  std::vector<std::string> arg(argv, argv + argc);
  for (unsigned i = 1; i < arg.size(); ++i) {
    if (arg[i] == "-c") {
      if (i+1 < arg.size()) {
        teamsize = string_to_<unsigned>(arg[i+1]);
        arg.erase(arg.begin() + i, arg.begin() + i + 2);  // Erase these two
        --i;                                              // Reset index
      } else {
        std::cerr << "-c option requires one argument." << std::endl;
        return 1;
      }
    }
    if (arg[i] == "-nocheck") {
      checkErrors = false;
      arg.erase(arg.begin() + i, arg.begin() + i + 1);  // Erase this arg
      --i;                                              // Reset index
    }
  }

  if (arg.size() < 2) {
    std::cerr << "Usage: " << arg[0] << " NUMPOINTS [-c TEAMSIZE] [-nocheck]" << std::endl;
    exit(1);
  }

  unsigned N = string_to_<int>(arg[1]);

  MPI_Init(&argc, &argv);
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int P;
  MPI_Comm_size(MPI_COMM_WORLD, &P);
  // Scratch request for MPI
  MPI_Request request = MPI_REQUEST_NULL;
  // Scratch status for MPI
  MPI_Status status;

  typedef InvSq kernel_type;
  kernel_type K;

  // Define source_type, target_type, charge_type, result_type
  typedef kernel_type::source_type source_type;
  typedef kernel_type::charge_type charge_type;
  typedef kernel_type::target_type target_type;
  typedef kernel_type::result_type result_type;

  // We are testing symmetric kernels
  static_assert(std::is_same<source_type, target_type>::value,
                "Testing symmetric kernels, need source_type == target_type");

  std::vector<source_type> source;
  std::vector<charge_type> charge;

  const int seed = 1337;

  if (rank == MASTER) {
    meta::default_generator.seed(seed);

    // generate source data
    for (unsigned i = 0; i < N; ++i)
      source.push_back(meta::random<source_type>::get());

    // generate charge data
    for (unsigned i = 0; i < N; ++i)
      charge.push_back(meta::random<charge_type>::get());

    // display metadata
    std::cout << "N = " << N << std::endl;
    std::cout << "P = " << P << std::endl;
    std::cout << "Teamsize = " << teamsize << std::endl;
  }


  ////////////////////////
  // Actual Computation //
  ////////////////////////
  // TODO: Factor out into function

  Clock timer;
  Clock compTimer;
  Clock splitTimer;
  Clock reduceTimer;
  Clock shiftTimer;
  Clock sendRecvTimer;

  double totalCompTime = 0;
  double totalSplitTime = 0;
  double totalReduceTime = 0;
  double totalShiftTime = 0;
  double totalSendRecvTime = 0;

  timer.start();

  // Broadcast the size of the problem to all processes
  //splitTimer.start();
  MPI_Bcast(&N, sizeof(N), MPI_CHAR, MASTER, MPI_COMM_WORLD);
  //totalSplitTime += splitTimer.elapsed();

  // Broadcast the teamsize to all processes
  //splitTimer.start();
  MPI_Bcast(&teamsize, sizeof(teamsize), MPI_CHAR, MASTER, MPI_COMM_WORLD);
  //totalSplitTime += splitTimer.elapsed();

  // TODO: How to generalize?
  if (N % P != 0) {
    printf("Quitting. The number of processors must divide the number of points\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(0);
  }

  if (P % teamsize != 0) {
    printf("Quitting. The teamsize (c) must divide the total number of processors (p).\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(0);
  }

  if (teamsize * teamsize > unsigned(P)) {
    printf("Quitting. The teamsize ^ 2 (c^2) must be less than or equal to the number of  processors (p).\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(0);
  }

  /***********/
  /** SETUP **/
  /***********/

  // Process data
  unsigned num_teams = P / teamsize;
  unsigned team  = rank / teamsize;
  unsigned trank = rank % teamsize;
  // Symmetric process placeholder
  int i_dst = -1;
  int i_src = -1;
  int r_dst = MPI_PROC_NULL;
  int r_src = MPI_PROC_NULL;

  // Split comm into row and column communicators
  MPI_Comm team_comm;
  MPI_Comm_split(MPI_COMM_WORLD, team, rank, &team_comm);
  MPI_Comm row_comm;
  MPI_Comm_split(MPI_COMM_WORLD, trank, rank, &row_comm);

  // Create transformer to help us convert between block idx and proc index
  IndexTransformer transposer(num_teams, teamsize);

  /*********************/
  /** BROADCAST STAGE **/
  /*********************/

  // Declare data for the block computations
  std::vector<source_type> xJ(idiv_up(N,num_teams));
  std::vector<charge_type> cJ(idiv_up(N,num_teams));
  std::vector<result_type> rJ(idiv_up(N,num_teams));

  // Scatter data from master to team leaders
  if (trank == MASTER) {
    //splitTimer.start();
    MPI_Scatter(source.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
                xJ.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
                MASTER, row_comm);
    MPI_Scatter(charge.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
                cJ.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
                MASTER, row_comm);
    //totalSplitTime += splitTimer.elapsed();
  }

  // Team leaders broadcast to team
  splitTimer.start();
  MPI_Bcast(xJ.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
            MASTER, team_comm);
  MPI_Bcast(cJ.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
            MASTER, team_comm);
  totalSplitTime += splitTimer.elapsed();

  // Copy xJ -> xI
  std::vector<source_type> xI = xJ;
  // Copy cJ -> cI
  std::vector<charge_type> cI = cJ;
  // Initialize block result rI
  std::vector<result_type> rI(idiv_up(N,num_teams));
  // Declare space for receiving
  std::vector<result_type> temp_rI(idiv_up(N,num_teams));

  // Perform initial offset by teamrank
  shiftTimer.start();
  int src = (team + trank + num_teams) % num_teams;
  int dst = (team - trank + num_teams) % num_teams;
  MPI_Sendrecv_replace(xJ.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
                       dst, 0, src, 0,
                       row_comm, &status);
  MPI_Sendrecv_replace(cJ.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
                       dst, 0, src, 0,
                       row_comm, &status);
  totalShiftTime += shiftTimer.elapsed();

  /**********************/
  /** ZEROTH ITERATION **/
  /**********************/

  int last_iter = idiv_up(num_teams + 1, 2*teamsize) - 1;
  int curr_iter = 0;   // Ranges from [0,last_iter]

  if (trank == MASTER) {
    // No destination for the symmetric send
    r_dst = MPI_PROC_NULL;

    // First iteration masters are computing the symmetric diagonal
    compTimer.start();
    p2p(K, xJ.begin(), xJ.end(), cJ.begin(), rI.begin());
    totalCompTime += compTimer.elapsed();
  } else {
    // Compute the symmetric iteration and rank
    std::tie(i_dst,r_dst) = transposer(curr_iter, team, trank);

    // If the block is the destination's last iteration, don't compute symm
    if (i_dst != last_iter) {
      // Compute symmetric off-diagonal
      compTimer.start();
      p2p(K,
          xJ.begin(), xJ.end(), cJ.begin(), rJ.begin(),
          xI.begin(), xI.end(), cI.begin(), rI.begin());
      totalCompTime += compTimer.elapsed();
    } else {
      // No destination for the symmetric send
      r_dst = MPI_PROC_NULL;

      // Compute asymmetric off-diagonal
      compTimer.start();
      p2p(K,
          xJ.begin(), xJ.end(), cJ.begin(),
          xI.begin(), xI.end(), rI.begin());
      totalCompTime += compTimer.elapsed();
    }
  }

  /********************/
  /** ALL ITERATIONS **/
  /********************/

  int iPrimeOffset = (trank == 0) ? 0 : 1;

  for (++curr_iter; curr_iter <= last_iter; ++curr_iter) {

    // The iteration of the block we would recv
    i_src = num_teams/teamsize - (curr_iter-1) - iPrimeOffset;
    // Compute the rank to receive from
    std::tie(std::ignore, r_src) = transposer(i_src, team, trank);

    // If the data we'd recv is from our last iteration or ourself, ignore it
    if (i_src == last_iter || r_src == rank)
      r_src = MPI_PROC_NULL;

    // Send/Recv the symmetric data from the last iteration
    sendRecvTimer.start();
    MPI_Sendrecv(rJ.data(), sizeof(result_type) * rJ.size(), MPI_CHAR,
                 r_dst, 0,
                 temp_rI.data(), sizeof(result_type) * temp_rI.size(), MPI_CHAR,
                 r_src, 0,
                 MPI_COMM_WORLD, &status);
    totalSendRecvTime += sendRecvTimer.elapsed();

    // Accumulate temp_rI to current answer
    if (r_src != MPI_PROC_NULL)
      for (auto r = rI.begin(), tr = temp_rI.begin(); r != rI.end(); ++r, ++tr)
        *r += *tr;


    // Shift data to the next process to compute the next block
    shiftTimer.start();
    int src = (team + teamsize + num_teams) % num_teams;
    int dst = (team - teamsize + num_teams) % num_teams;
    MPI_Sendrecv_replace(xJ.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
                         dst, 0, src, 0,
                         row_comm, &status);
    MPI_Sendrecv_replace(cJ.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
                         dst, 0, src, 0,
                         row_comm, &status);
    totalShiftTime += shiftTimer.elapsed();


    // Compute the destination iteration and rank
    std::tie(i_dst, r_dst) = transposer(curr_iter, team, trank);

    // If the block is the destination's last iteration, don't compute symm
    //assert(i_dst > curr_iter || curr_iter == last_iter-1);
    if (i_dst != last_iter) {
      // Set rJ to zero
      std::fill(rJ.begin(), rJ.end(), result_type());

      // Compute symmetric off-diagonal
      compTimer.start();
      p2p(K,
          xJ.begin(), xJ.end(), cJ.begin(), rJ.begin(),
          xI.begin(), xI.end(), cI.begin(), rI.begin());
      totalCompTime += compTimer.elapsed();
    } else {
      // No destination for the symmetric send
      r_dst = MPI_PROC_NULL;

      // Compute asymmetric off-diagonal
      compTimer.start();
      p2p(K,
          xJ.begin(), xJ.end(), cJ.begin(),
          xI.begin(), xI.end(), rI.begin());
      totalCompTime += compTimer.elapsed();

      r_dst = MPI_PROC_NULL;
    }

  }  //  end for iteration

  /********************/
  /*** REDUCE STAGE ***/
  /********************/

  // Reduce answers to the team leader
  reduceTimer.start();
  // TODO: Generalize
  static_assert(std::is_same<result_type, double>::value,
                "Need result_type == double for now");
  MPI_Reduce(rI.data(), temp_rI.data(), rI.size(), MPI_DOUBLE,
             MPI_SUM, MASTER, team_comm);
  totalReduceTime += reduceTimer.elapsed();

  // Allocate result on master
  std::vector<result_type> result;
  if (rank == MASTER)
    result = std::vector<result_type>(P*idiv_up(N,P));

  // Gather team leader answers to master
  if (trank == MASTER) {
    //reduceTimer.start();
    MPI_Gather(temp_rI.data(), sizeof(result_type) * temp_rI.size(), MPI_CHAR,
               result.data(), sizeof(result_type) * temp_rI.size(), MPI_CHAR,
               MASTER, row_comm);
    //totalReduceTime += reduceTimer.elapsed();
  }


  double time = timer.elapsed();

  // Collect times to MASTER

  // Receive buffers for master
  double avgCompTime = 0;
  double avgSplitTime = 0;
  double avgShiftTime = 0;
  double avgReduceTime = 0;
  double avgSendRecvTime = 0;

  // Could use all reduce here to get the averaged data to all the processors
  MPI_Reduce(&totalCompTime, &avgCompTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  MPI_Reduce(&totalSplitTime, &avgSplitTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  MPI_Reduce(&totalShiftTime, &avgShiftTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  MPI_Reduce(&totalSendRecvTime, &avgSendRecvTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  MPI_Reduce(&totalReduceTime, &avgReduceTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);

  avgCompTime     /= P;
  avgSplitTime    /= P;
  avgShiftTime    /= P;
  avgSendRecvTime /= P;
  avgReduceTime   /= P;


  // format output well
  if (rank == MASTER) {
    printf("Label\tComputation\tSplit\tShift\tSendReceive\tReduce\n");
    printf("C=%d\t%e\t%e\t%e\t%e\t%e\n", teamsize, avgCompTime, avgSplitTime, avgShiftTime, avgSendRecvTime, avgReduceTime);
    printf("Rank 0 Total Time: %e\n", time);
  }

  // Check the result
  if (rank == MASTER && checkErrors) {
    std::string result_filename = "data/";
    result_filename += std::string("invsq")
        + "_n" + std::to_string(N)
        + "_s" + std::to_string(seed) + ".txt";

    std::fstream result_file(result_filename);

    if (result_file) {
      std::cout << "Reading result from " << result_filename << std::endl;

      // Read the previously computed results
      std::vector<result_type> exact;
      result_file >> exact;
      assert(exact.size() == N);

      print_error(exact, result);
    } else {
      std::cout << "Computing direct matvec..." << std::endl;

      std::vector<result_type> exact(N);

      // Compute the result with a direct matrix-vector multiplication
      compTimer.start();
      p2p(K, source.begin(), source.end(), charge.begin(), exact.begin());
      double directCompTime = compTimer.elapsed();

      print_error(exact, result);
      std::cout << "DirectCompTime: " << directCompTime << std::endl;

      // Open and write
      result_file.open(result_filename, std::fstream::out);
      result_file << exact << std::endl;
    }
  }

  MPI_Finalize();
  return 0;
}

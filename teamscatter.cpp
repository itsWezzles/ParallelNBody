#include "Util.hpp"

#include "kernel/InvSq.kern"
#include "meta/kernel_traits.hpp"
#include "meta/random.hpp"

// Team Scatter version of the n-body algorithm

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

  srand(time(NULL));
  unsigned N = string_to_<int>(arg[1]);

  MPI_Init(&argc, &argv);
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int P;
  MPI_Comm_size(MPI_COMM_WORLD, &P);
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

  double totalCompTime = 0;
  double totalSplitTime = 0;
  double totalReduceTime = 0;
  double totalShiftTime = 0;

  timer.start();

  // Broadcast the size of the problem to all processes
  splitTimer.start();
  MPI_Bcast(&N, sizeof(N), MPI_CHAR, MASTER, MPI_COMM_WORLD);
  totalSplitTime += splitTimer.elapsed();

  // Broadcast the teamsize to all processes
  splitTimer.start();
  MPI_Bcast(&teamsize, sizeof(teamsize), MPI_CHAR, MASTER, MPI_COMM_WORLD);
  totalSplitTime += splitTimer.elapsed();

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

  unsigned num_teams = P / teamsize;
  // Determine coordinates in processor team grid
  unsigned team  = rank / teamsize;
  unsigned trank = rank % teamsize;

  // Split comm into row and column communicators
  MPI_Comm team_comm;
  MPI_Comm_split(MPI_COMM_WORLD, team, rank, &team_comm);
  MPI_Comm row_comm;
  MPI_Comm_split(MPI_COMM_WORLD, trank, rank, &row_comm);

  /*********************/
  /** BROADCAST STAGE **/
  /*********************/

  // Declare data for the block computations
  std::vector<source_type> xJ(idiv_up(N,num_teams));
  std::vector<charge_type> cJ(idiv_up(N,num_teams));

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
  // Initialize block result rI
  std::vector<result_type> rI(idiv_up(N,num_teams));

  // Perform initial offset by teamrank
  shiftTimer.start();
  int dst = (team + trank + num_teams) % num_teams;
  int src = (team - trank + num_teams) % num_teams;
  MPI_Sendrecv_replace(xJ.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
                       src, 0, dst, 0,
                       row_comm, &status);
  MPI_Sendrecv_replace(cJ.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
                       src, 0, dst, 0,
                       row_comm, &status);
  totalShiftTime += shiftTimer.elapsed();

  /**********************/
  /** ZEROTH ITERATION **/
  /**********************/

  int last_iter = idiv_up(P, teamsize*teamsize) - 1;
  int curr_iter = 0;   // Ranges from [0,last_iter]

  if (trank == MASTER) {
    // If this is the team leader, compute the symmetric diagonal block
    compTimer.start();
    p2p(K, xJ.begin(), xJ.end(), cJ.begin(), rI.begin());
    totalCompTime += compTimer.elapsed();
  } else {
    // Else, compute the off-diagonal block
    compTimer.start();
    p2p(K,
        xJ.begin(), xJ.end(), cJ.begin(),
        xI.begin(), xI.end(), rI.begin());
    totalCompTime += compTimer.elapsed();
  }

  /********************/
  /** ALL ITERATIONS **/
  /********************/

  // Looping process to shift the data between the teams
  for (++curr_iter; curr_iter <= last_iter; ++curr_iter) {

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

    // Compute on the last iteration only if
    // 1) The teamsize divides the number of teams (everyone computes)
    // 2) Your team rank is one of the remainders
    if (curr_iter < last_iter
        || (num_teams % teamsize == 0 || trank < num_teams % teamsize)) {
      compTimer.start();
      p2p(K,
          xJ.begin(), xJ.end(), cJ.begin(),
          xI.begin(), xI.end(), rI.begin());
      totalCompTime += compTimer.elapsed();
    }
  }

  /********************/
  /*** REDUCE STAGE ***/
  /********************/

  // Allocate teamrI on team leaders
  std::vector<result_type> teamrI;
  if (trank == MASTER)
    teamrI = std::vector<result_type>(idiv_up(N,num_teams));

  // Reduce answers to the team leader
  reduceTimer.start();
  // TODO: Generalize
  static_assert(std::is_same<result_type, double>::value,
                "Need result_type == double for now");
  MPI_Reduce(rI.data(), teamrI.data(), rI.size(), MPI_DOUBLE,
             MPI_SUM, MASTER, team_comm);
  totalReduceTime += reduceTimer.elapsed();

  // Allocate result on master
  std::vector<result_type> result;
  if (rank == MASTER)
    result = std::vector<result_type>(P*idiv_up(N,P));

  // Gather team leader answers to master
  if (trank == MASTER) {
    //reduceTimer.start();
    MPI_Gather(teamrI.data(), sizeof(result_type) * teamrI.size(), MPI_CHAR,
               result.data(), sizeof(result_type) * teamrI.size(), MPI_CHAR,
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

  // Could use all reduce here to get the averaged data to all the processors
  MPI_Reduce(&totalCompTime, &avgCompTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  avgCompTime /= P;

  MPI_Reduce(&totalSplitTime, &avgSplitTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  avgSplitTime /= P;

  MPI_Reduce(&totalShiftTime, &avgShiftTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  avgShiftTime /= P;

  MPI_Reduce(&totalReduceTime, &avgReduceTime, 1, MPI_DOUBLE,
             MPI_SUM, MASTER, MPI_COMM_WORLD);
  avgReduceTime /= P;

  // format output well
  if (rank == MASTER) {
    printf("Label\tComputation\tSplit\tShift\tReduce\n");
    printf("c=%d\t%e\t%e\t%e\t%e\n", teamsize, avgCompTime, avgSplitTime, avgShiftTime, avgReduceTime);
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

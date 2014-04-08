#include "Util.hpp"

// Scatter version of the n-body algorithm

#include "kernel/NonParaBayesian.kern"
#include "meta/kernel_traits.hpp"

int main(int argc, char** argv)
{
  bool checkErrors = true;

  // Parse optional command line args
  std::vector<std::string> arg(argv, argv + argc);
  for (unsigned i = 1; i < arg.size(); ++i) {
    if (arg[i] == "-nocheck") {
      checkErrors = false;
      arg.erase(arg.begin() + i);  // Erase this arg
      --i;                         // Reset index
    }
  }

  MPI_Init(&argc, &argv);
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int P;
  MPI_Comm_size(MPI_COMM_WORLD, &P);
  // Scratch status for MPI
  MPI_Status status;

  typedef NonParaBayesian kernel_type;
  kernel_type K(1,1);

  // Define source_type, target_type, charge_type, result_type
  typedef kernel_type::source_type source_type;
  typedef kernel_type::charge_type charge_type;
  typedef kernel_type::target_type target_type;
  typedef kernel_type::result_type result_type;

  std::vector<source_type> source;
  std::vector<charge_type> charge;
  unsigned N;

  Clock timer;
  Clock commTimer;
  double totalCommTime = 0;

  if (rank == MASTER) {
    if (arg.size() < 3) {
      std::cerr << "Usage: " << arg[0] << " SOURCE_FILE CHARGE_FILE" << std::endl;
      //exit(1);
      // XXX: Remove
      std::cerr << "Using default " << SOURCE_DATA << " " << CHARGE_DATA << std::endl;

      arg.resize(1);
      arg.push_back(SOURCE_DATA);
      arg.push_back(CHARGE_DATA);
    }

    // Read the data from SOURCE_FILE interpreted as Points
    std::ifstream source_file(arg[1]);
    source_file >> source;

    // Read the data from CHARGE_FILE interpreted as doubles
    std::ifstream charge_file(arg[2]);
    charge_file >> charge;

    assert(source.size() == charge.size());
    N = charge.size();
    std::cout << "N = " << N << std::endl;
    std::cout << "P = " << P << std::endl;

    // Pad with garbage values if needed
    source.resize(P * idiv_up(N,P));
    charge.resize(P * idiv_up(N,P));
  }

  // Broadcast the size of the problem to all processes
  timer.start();
  commTimer.start();
  MPI_Bcast(&N, sizeof(N), MPI_CHAR, MASTER, MPI_COMM_WORLD);
  totalCommTime += commTimer.elapsed();

  // TODO: Generalize by excluding the garbage values
  if (N % P != 0) {
    printf("Quitting. The number of processors must divide the total number of tasks.\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(0);
  }

  // Allocate memory all processes
  std::vector<source_type> xJ(idiv_up(N,P));
  std::vector<charge_type> cJ(idiv_up(N,P));

  // Scatter the data to all processes
  commTimer.start();
  MPI_Scatter(source.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
              xJ.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
              MASTER, MPI_COMM_WORLD);
  MPI_Scatter(charge.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
              cJ.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
              MASTER, MPI_COMM_WORLD);
  totalCommTime += commTimer.elapsed();

  // Copy xJ -> xI
  std::vector<source_type> xI = xJ;
  // Initialize block results rI
  std::vector<result_type> rI(idiv_up(N,P));

  // Calculate the symmetric first block
  p2p(K, xJ.begin(), xJ.end(), cJ.begin(), rI.begin());

  for (int shiftCount = 1; shiftCount < P; ++shiftCount) {
    commTimer.start();

    int dst = (rank - 1 + P) % P;
    int src = (rank + 1 + P) % P;
    MPI_Sendrecv_replace(xJ.data(), sizeof(source_type) * xJ.size(), MPI_CHAR,
                         src, 0, dst, 0,
                         MPI_COMM_WORLD, &status);
    MPI_Sendrecv_replace(cJ.data(), sizeof(charge_type) * cJ.size(), MPI_CHAR,
                         src, 0, dst, 0,
                         MPI_COMM_WORLD, &status);
    totalCommTime += commTimer.elapsed();

    // Calculate the current block
    p2p(K,
        xJ.begin(), xJ.end(), cJ.begin(),
        xI.begin(), xI.end(), rI.begin());
  }

  std::vector<result_type> result;
  if (rank == MASTER)
    result = std::vector<result_type>(P*idiv_up(N,P));

  // Collect results and display
  commTimer.start();
  MPI_Gather(rI.data(), sizeof(result_type) * rI.size(), MPI_CHAR,
             result.data(), sizeof(result_type) * rI.size(), MPI_CHAR,
             MASTER, MPI_COMM_WORLD);
  totalCommTime += commTimer.elapsed();

  double time = timer.elapsed();
  printf("[%d] Timer: %e\n", rank, time);
  printf("[%d] CommTimer: %e\n", rank, totalCommTime);

  // Check the result
  if (rank == MASTER && checkErrors) {
    std::cout << "Computing direct matvec..." << std::endl;

    std::vector<result_type> exact(N);

    // Compute the result with a direct matrix-vector multiplication
    p2p(K, source.begin(), source.end(), charge.begin(), exact.begin());

    print_error(exact, result);
  }

  if (rank == MASTER) {
    std::ofstream result_file("data/result.txt");
    result_file << result << std::endl;
  }

  MPI_Finalize();
  return 0;
}

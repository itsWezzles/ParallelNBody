#include "Util.hpp"
#include "Vec.hpp"

// Scatter version of the n-body algorithm

int main(int argc, char** argv)
{
  MPI_Init(&argc, &argv);
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int P;
  MPI_Comm_size(MPI_COMM_WORLD, &P);

  typedef Vec<3,double> Point;
  std::vector<Point> data;
  std::vector<double> sigma;
  unsigned N;

  Clock timer;
  Clock commTimer;
  double totalCommTime = 0;

  if (rank == MASTER) {
    std::vector<std::string> arg(argv, argv+argc);

    if (arg.size() < 3) {
      std::cerr << "Usage: " << arg[0] << " PHI_FILE SIGMA_FILE" << std::endl;
      //exit(1);
      // XXX: Remove
      std::cerr << "Using default " << PHIDATA << " " << SIGMADATA << std::endl;

      arg.resize(1);
      arg.push_back(PHIDATA);
      arg.push_back(SIGMADATA);
    }

    // Read the data from PHI_FILE interpreted as Points
    std::ifstream data_file(arg[1]);
    data_file >> data;

    // Read the data from SIGMA_FILE interpreted as doubles
    std::ifstream sigma_file(arg[2]);
    sigma_file >> sigma;

    assert(data.size() == sigma.size());
    N = sigma.size();
    std::cout << "N = " << N << std::endl;
    std::cout << "P = " << P << std::endl;
  }

  // Broadcast the size of the problem to all processes
  timer.start();
  commTimer.start();
  MPI_Bcast(&N, 1, MPI_UNSIGNED, MASTER, MPI_COMM_WORLD);
  totalCommTime += commTimer.elapsed();

  // TODO: How to generalize?
  if (N % P != 0) {
    printf("Quitting. The number of processors must divide the total number of tasks.\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(0);
  }

  // Allocate memory all processes
  std::vector<double> phiI(idiv_up(N,P));
  std::vector<Point> xI(idiv_up(N,P));
  std::vector<Point> xJ(idiv_up(N,P));
  std::vector<double> sigmaJ(idiv_up(N,P));

  // Scatter the data to all processes
  unsigned count = Point::size() * N;
  commTimer.start();
  MPI_Scatter(data.data(), idiv_up(count,P), MPI_DOUBLE,
              xI.data(), idiv_up(count,P), MPI_DOUBLE,
              MASTER, MPI_COMM_WORLD);
  MPI_Scatter(sigma.data(), idiv_up(N,P), MPI_DOUBLE,
              sigmaJ.data(), idiv_up(N,P), MPI_DOUBLE,
              MASTER, MPI_COMM_WORLD);
  totalCommTime += commTimer.elapsed();

  // Copy xI -> xJ
  std::copy(xI.begin(), xI.end(), xJ.begin());

  // Calculate the first block
  block_eval(xJ.begin(), xJ.end(), sigmaJ.begin(),
             xI.begin(), xI.end(), phiI.begin());

  MPI_Status status;
  for (int shiftCount = 1; shiftCount < P; ++shiftCount) {
    commTimer.start();
    // Add P to prevent negative numbers
    int prev  = (rank - 1 + P) % P;
    int next = (rank + 1 + P) % P;
    MPI_Sendrecv_replace(xJ.data(), idiv_up(count,P), MPI_DOUBLE,
                         next, 0, prev, 0,
                         MPI_COMM_WORLD, &status);
    MPI_Sendrecv_replace(sigmaJ.data(), idiv_up(N,P), MPI_DOUBLE,
                         next, 0, prev, 0,
                         MPI_COMM_WORLD, &status);

    totalCommTime += commTimer.elapsed();

    // Calculate the current block
    block_eval(xJ.begin(), xJ.end(), sigmaJ.begin(),
               xI.begin(), xI.end(), phiI.begin());
  }

  std::vector<double> phi;
  if (rank == MASTER)
    phi = std::vector<double>(P*idiv_up(N,P));

  // Collect results and display
  commTimer.start();
  MPI_Gather(phiI.data(), idiv_up(N,P), MPI_DOUBLE,
             phi.data(), idiv_up(N,P), MPI_DOUBLE,
             MASTER, MPI_COMM_WORLD);
  totalCommTime += commTimer.elapsed();

  double time = timer.elapsed();
  printf("[%d] Timer: %e\n", rank, time);
  printf("[%d] CommTimer: %e\n", rank, totalCommTime);

  if (rank == MASTER) {
    double checkSum = std::accumulate(phi.begin(), phi.end(), 0.0);
    std::cout << "Scatter - checksum answer is: " << checkSum << std::endl;
    std::ofstream phi_file("data/phi.txt");
    phi_file << phi << std::endl;
  }

  MPI_Finalize();
  return 0;
}

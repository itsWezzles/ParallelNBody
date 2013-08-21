#include "Util.hpp"

#include "Vec.hpp"

// Serial version of n-body algorithm

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " PHI_FILE SIGMA_FILE" << std::endl;
    //exit(1);
    // XXX: Remove
    std::cerr << "Using default " << PHIDATA << " " << SIGMADATA << std::endl;
    argc = 3;
    char** new_argv = new char*[3];
    new_argv[0] = argv[0];
    new_argv[1] = (char*)& PHIDATA;
    new_argv[2] = (char*)& SIGMADATA;
    argv = new_argv;
  }

  typedef Vec<3,double> Point;
  std::vector<Point> data;
  std::vector<double> sigma;

  // Read the data from PHI_FILE interpreted as Points
  std::ifstream data_file(argv[1]);
  data_file >> data;

  // Read the data from SIGMA_FILE interpreted as doubles
  std::ifstream sigma_file(argv[2]);
  sigma_file >> sigma;

  // Make sure this makes sense and get metadata
  assert(data.size() == sigma.size());
  unsigned N = sigma.size();
  std::cout << "N = " << N << std::endl;

  // Compute the matvec
  std::vector<double> phi(N, 0);

  Clock timer;
  timer.start();
  block_eval(data.begin(), data.end(), sigma.begin(),
             data.begin(), data.end(), phi.begin());
  double time = timer.elapsed();

  std::cout << "Computed in " << time << " seconds" << std::endl;
  double checkSum = std::accumulate(phi.begin(), phi.end(), 0.0);
  std::cout << "Serial - checksum answer is: " << checkSum << std::endl;

  std::ofstream phi_file("data/phi.txt");
  phi_file << phi << std::endl;
}

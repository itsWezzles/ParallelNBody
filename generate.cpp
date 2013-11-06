#include "Util.hpp"
#include "Vec.hpp"

#include "meta/random.hpp"

int main(int argc, char** argv)
{
  std::vector<std::string> arg(argv, argv+argc);

  if (arg.size() < 3) {
    std::cerr << "Usage: " << arg[0] << " PHI_FILE SIGMA_FILE N" << std::endl;
    //exit(1);
    // XXX: Remove
    std::cerr << "Using default "
              << PHIDATA << " " << SIGMADATA << " " << NUMPOINTS << std::endl;

    arg.resize(1);
    arg.push_back(PHIDATA);
    arg.push_back(SIGMADATA);
    arg.push_back(to_string(NUMPOINTS));
  }

  srand(time(NULL));
  unsigned N = string_to_<int>(arg[3]);

  typedef Vec<3,double> source_type;
  std::ofstream data(arg[1]);
  for (unsigned i = 0; i < N; ++i)
    data << meta::random<source_type>::get() << std::endl;

  typedef double        charge_type;
  std::ofstream sigma(arg[2]);
  for (unsigned i = 0; i < N; ++i)
    sigma << meta::random<charge_type>::get() << std::endl;
}

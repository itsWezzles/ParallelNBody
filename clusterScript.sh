#!/bin/bash
#SBATCH -n 32                 						#Number of cores
#SBATCH -t 10                 						#Runtime in minutes
#SBATCH -p general   								#Partition to submit to
#SBATCH --mem-per-cpu=100   						#Memory per cpu in MB (see also --mem)
#SBATCH -o hopper.out     					#File to which standard out will be written
#SBATCH -e hopper.err      					#File to which standard err will be written
 
#
# Use modules to setup the runtime environment
#
. /etc/profile
module load centos6/openmpi-1.7.2_intel-13.0.079
module load centos6/fftw-3.3.3_openmpi-1.6.4_gcc-4.8.0
 
#
# Execute the run
#

mpirun -np 32 ./symmetric 20000  -c 1

mpirun -np 32 ./symmetric 20000 -c 2 -nocheck

mpirun -np 32 ./symmetric 20000 -c 4 -nocheck

#mpirun -np 32 ./symmetric 20000 -c 8 -nocheck

mpirun -np 32 ./teamscatter 20000 -c 1 -nocheck

mpirun -np 32 ./teamscatter 20000 -c 2 -nocheck

mpirun -np 32 ./teamscatter 20000 -c 4 -nocheck

#mpirun -np 32 ./teamscatter 20000 -c 8 -nocheck
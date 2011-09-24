/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "string.h"
#include "ctype.h"
#include "fix.h"
#include "atom.h"
#include "group.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

Fix::Fix(LAMMPS *lmp, int narg, char **arg) : Pointers(lmp)
{
  // fix ID, group, and style
  // ID must be all alphanumeric chars or underscores

  int n = strlen(arg[0]) + 1;
  id = new char[n];
  strcpy(id,arg[0]);

  for (int i = 0; i < n-1; i++)
    if (!isalnum(id[i]) && id[i] != '_')
      error->all(FLERR,"Fix ID must be alphanumeric or underscore characters");

  igroup = group->find(arg[1]);
  if (igroup == -1) error->all(FLERR,"Could not find fix group ID");
  groupbit = group->bitmask[igroup];

  n = strlen(arg[2]) + 1;
  style = new char[n];
  strcpy(style,arg[2]);

  restart_global = 0;
  restart_peratom = 0;
  force_reneighbor = 0;
  box_change = 0;
  box_change_size = 0;
  box_change_shape = 0;
  thermo_energy = 0;
  rigid_flag = 0;
  virial_flag = 0;
  no_change_box = 0;
  time_integrate = 0;
  time_depend = 0;
  create_attribute = 0;
  restart_pbc = 0;
  cudable_comm = 0;

  scalar_flag = vector_flag = array_flag = 0;
  peratom_flag = local_flag = 0;

  comm_forward = comm_reverse = 0;

  maxvatom = 0;
  vatom = NULL;

  // mask settings - same as in modify.cpp

  INITIAL_INTEGRATE = 1;
  POST_INTEGRATE = 2;
  PRE_EXCHANGE = 4;
  PRE_NEIGHBOR = 8;
  PRE_FORCE = 16;
  POST_FORCE = 32;
  FINAL_INTEGRATE = 64;
  END_OF_STEP = 128;
  THERMO_ENERGY = 256;
  INITIAL_INTEGRATE_RESPA = 512;
  POST_INTEGRATE_RESPA = 1024;
  PRE_FORCE_RESPA = 2048;
  POST_FORCE_RESPA = 4096;
  FINAL_INTEGRATE_RESPA = 8192;
  MIN_PRE_EXCHANGE = 16384;
  MIN_PRE_FORCE = 32768;
  MIN_POST_FORCE = 65536;
  MIN_ENERGY = 131072;
  POST_RUN = 262144;
}

/* ---------------------------------------------------------------------- */

Fix::~Fix()
{
  delete [] id;
  delete [] style;
  memory->destroy(vatom);
}

/* ----------------------------------------------------------------------
   process params common to all fixes here
   if unknown param, call modify_param specific to the fix
------------------------------------------------------------------------- */

void Fix::modify_params(int narg, char **arg)
{
  if (narg == 0) error->all(FLERR,"Illegal fix_modify command");

  int iarg = 0;
  while (iarg < narg) {
    if (strcmp(arg[iarg],"energy") == 0) {
      if (iarg+2 > narg) error->all(FLERR,"Illegal fix_modify command");
      if (strcmp(arg[iarg+1],"no") == 0) thermo_energy = 0;
      else if (strcmp(arg[iarg+1],"yes") == 0) thermo_energy = 1;
      else error->all(FLERR,"Illegal fix_modify command");
      iarg += 2;
    } else {
      int n = modify_param(narg-iarg,&arg[iarg]);
      if (n == 0) error->all(FLERR,"Illegal fix_modify command");
      iarg += n;
    }
  }
}

/* ----------------------------------------------------------------------
   setup for virial computation
   see integrate::ev_set() for values of vflag (0-6)
------------------------------------------------------------------------- */

void Fix::v_setup(int vflag)
{
  int i,n;

  evflag = 1;

  vflag_global = vflag % 4;
  vflag_atom = vflag / 4;

  // reallocate per-atom array if necessary
  
  if (vflag_atom && atom->nlocal > maxvatom) {
    maxvatom = atom->nmax;
    memory->destroy(vatom);
    memory->create(vatom,maxvatom,6,"bond:vatom");
  }

  // zero accumulators

  if (vflag_global) for (i = 0; i < 6; i++) virial[i] = 0.0;
  if (vflag_atom) {
    n = atom->nlocal;
    for (i = 0; i < n; i++) {
      vatom[i][0] = 0.0;
      vatom[i][1] = 0.0;
      vatom[i][2] = 0.0;
      vatom[i][3] = 0.0;
      vatom[i][4] = 0.0;
      vatom[i][5] = 0.0;
    }
  }
}

/* ----------------------------------------------------------------------
   tally virial into global and per-atom accumulators
   v = total virial for the interaction involving total atoms
   n = # of local atoms involved, with local indices in list
   increment global virial by n/total fraction
   increment per-atom virial of each atom in list by 1/total fraction
   assumes other procs will tally left-over fractions
------------------------------------------------------------------------- */

void Fix::v_tally(int n, int *list, double total, double *v)
{
  int m;

  if (vflag_global) {
    double fraction = n/total;
    virial[0] += fraction*v[0];
    virial[1] += fraction*v[1];
    virial[2] += fraction*v[2];
    virial[3] += fraction*v[3];
    virial[4] += fraction*v[4];
    virial[5] += fraction*v[5];
  }

  if (vflag_atom) {
    double fraction = 1.0/total;
    for (int i = 0; i < n; i++) {
      m = list[i];
      vatom[m][0] += fraction*v[0];
      vatom[m][1] += fraction*v[1];
      vatom[m][2] += fraction*v[2];
      vatom[m][3] += fraction*v[3];
      vatom[m][4] += fraction*v[4];
      vatom[m][5] += fraction*v[5];
    }
  }
}

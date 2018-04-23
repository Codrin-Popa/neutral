#include "../comms.h"
#include "../mesh.h"
#include "../params.h"
#include "../profiler.h"
#include "../shared_data.h"
#include "neutral_interface.h"
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MPI
#include "mpi.h"
#endif

void plot_particle_density(NeutralData* neutral_data, Mesh* mesh, const int tt,
                           const int nparticles, const double elapsed_sim_time);

int main(int argc, char** argv) {
  if (argc != 2) {
    TERMINATE("usage: ./neutral.exe <param_file>\n");
  }

  // Store the dimensions of the mesh
  Mesh mesh;
  NeutralData neutral_data;
  neutral_data.neutral_params_filename = argv[1];
  mesh.global_nx =
      get_int_parameter("nx", neutral_data.neutral_params_filename);
  mesh.global_ny =
      get_int_parameter("ny", neutral_data.neutral_params_filename);
  mesh.pad = 0;
  mesh.local_nx = mesh.global_nx + 2 * mesh.pad;
  mesh.local_ny = mesh.global_ny + 2 * mesh.pad;
  mesh.width = get_double_parameter("width", ARCH_ROOT_PARAMS);
  mesh.height = get_double_parameter("height", ARCH_ROOT_PARAMS);
  mesh.dt = get_double_parameter("dt", neutral_data.neutral_params_filename);
  mesh.sim_end = get_double_parameter("sim_end", ARCH_ROOT_PARAMS);
  mesh.niters =
      get_int_parameter("iterations", neutral_data.neutral_params_filename);
  mesh.rank = MASTER;
  mesh.nranks = 1;
  mesh.ndims = 2;
  const int visit_dump =
      get_int_parameter("visit_dump", neutral_data.neutral_params_filename);

// Get the number of threads and initialise the random number pool
#pragma omp parallel
  { neutral_data.nthreads = omp_get_num_threads(); }

  printf("Starting up with %d OpenMP threads.\n", neutral_data.nthreads);
  printf("Loading problem from %s.\n", neutral_data.neutral_params_filename);
#ifdef ENABLE_PROFILING
  /* The timing code has to be called so many times that the API calls
   * actually begin to influence the performance dramatically. */
  fprintf(stderr,
          "Warning. Profiling is enabled and will increase the runtime.\n\n");
#endif

  // Perform the general initialisation steps for the mesh etc
  uint64_t master_key = 0;
  initialise_mpi(argc, argv, &mesh.rank, &mesh.nranks);
  initialise_devices(mesh.rank);
  initialise_comms(&mesh);
  initialise_mesh_2d(&mesh);
  SharedData shared_data = {0};
  initialise_shared_data_2d(mesh.local_nx, mesh.local_ny, mesh.pad, mesh.width, 
      mesh.height, neutral_data.neutral_params_filename, mesh.edgex, mesh.edgey, &shared_data);

  handle_boundary_2d(mesh.local_nx, mesh.local_ny, &mesh, shared_data.density,
                     NO_INVERT, PACK);
  initialise_neutral_data(&neutral_data, &mesh, &shared_data, master_key++);

  // Make sure initialisation phase is complete
  barrier();

  // Main timestep loop where we will track each particle through time
  int tt;
  double wallclock = 0.0;
  double elapsed_sim_time = 0.0;
  for (tt = 1; tt <= mesh.niters; ++tt) {

    if (mesh.rank == MASTER) {
      printf("\nIteration  %d\n", tt);
    }

    uint64_t facet_events = 0;
    uint64_t collision_events = 0;

    double w0 = omp_get_wtime();

    // Begin the main solve step
    solve_transport_2d(
        mesh.local_nx - 2 * mesh.pad, mesh.local_ny - 2 * mesh.pad,
        mesh.global_nx, mesh.global_ny, mesh.pad, mesh.x_off, mesh.y_off,
        (float)mesh.dt, neutral_data.nparticles, &neutral_data.nlocal_particles,
        &master_key, mesh.neighbours, neutral_data.local_particles,
        neutral_data.density, neutral_data.edgex, neutral_data.edgey, 
        neutral_data.edgedx, neutral_data.edgedy,
        neutral_data.cs_scatter_table, neutral_data.cs_absorb_table,
        neutral_data.energy_deposition_tally, neutral_data.nfacets_reduce_array,
        neutral_data.ncollisions_reduce_array, neutral_data.nprocessed_reduce_array,
        &facet_events, &collision_events);

    barrier();

    double step_time = omp_get_wtime() - w0;
    wallclock += step_time;
    printf("Facets     %lu\n", facet_events);
    printf("Collisions %lu\n", collision_events);
    printf("Step time  %.4fs\n", step_time);
    printf("Wallclock  %.4fs\n", wallclock);

    printf("Collision Events / s = %.2e\n", (collision_events / step_time));
    printf("Facet Events / s = %.2e\n", (facet_events / step_time));

    elapsed_sim_time += mesh.dt;

    if (visit_dump) {
#if 0
      char tally_name[100];
      sprintf(tally_name, "energy%d", tt);
      int dneighbours[NNEIGHBOURS] = {EDGE, EDGE, EDGE, EDGE, EDGE, EDGE};
      write_all_ranks_to_visit(
          mesh.global_nx, mesh.global_ny, mesh.local_nx - 2 * mesh.pad,
          mesh.local_ny - 2 * mesh.pad, mesh.pad, mesh.x_off, mesh.y_off,
          mesh.rank, mesh.nranks, dneighbours,
          neutral_data.energy_deposition_tally, tally_name, 0,
          elapsed_sim_time);
#endif // if 0
    }

    // Leave the simulation if we have reached the simulation end time
    if (elapsed_sim_time >= mesh.sim_end) {
      if (mesh.rank == MASTER)
        printf("Reached end of simulation time\n");
      break;
    }
  }

  validate(mesh.local_nx - 2 * mesh.pad, mesh.local_ny - 2 * mesh.pad,
      neutral_data.neutral_params_filename, mesh.rank,
      neutral_data.energy_deposition_tally);

  if (mesh.rank == MASTER) {
    printf("Final Wallclock %.9fs\n", wallclock);
    printf("Elapsed Simulation Time %.6fs\n", elapsed_sim_time);
  }

  return 0;
}

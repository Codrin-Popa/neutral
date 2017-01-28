#include <math.h>
#include <stdio.h>
#include "../mt19937.h"
#include "../bright_interface.h"
#include "../../comms.h"
#include "../../shared.h"
#include "../../shared_data.h"
#include "bright.h"
#include <assert.h>

#ifdef MPI
#include "mpi.h"
#endif

// Performs a solve of dependent variables for particle transport.
void solve_transport_2d(
    const int nx, const int ny, const int global_nx, const int global_ny, 
    const int x_off, const int y_off, const double dt, int* nlocal_particles, 
    const int* neighbours, Particle* particles, const double* density, 
    const double* edgex, const double* edgey, Particle* out_particles, 
    CrossSection* cs_scatter_table, CrossSection* cs_absorb_table, 
    double* energy_tally)
{
  // Initial idea is to use a kind of queue for handling the particles. Presumably
  // this doesn't have to be a carefully ordered queue but lets see how that goes.

  // This is the known starting number of particles
  int facets = 0;
  int collisions = 0;
  int nparticles = *nlocal_particles;

  // Communication isn't required for edges
  int nparticles_sent[NNEIGHBOURS];
  for(int ii = 0; ii < NNEIGHBOURS; ++ii) {
    nparticles_sent[ii] = 0;
  }

  // Set initial dt for all particles
  for(int ii = 0; ii < nparticles; ++ii) {
    particles[ii].dt_to_census = dt;
    particles[ii].mfp_to_collision = 0.0;
  }

  handle_particles(
      global_nx, global_ny, nx, ny, x_off, y_off, dt, neighbours,
      density, edgex, edgey, &facets, &collisions, nparticles_sent, nparticles,
      &nparticles, particles, out_particles, cs_scatter_table, cs_absorb_table,
      energy_tally);

#ifdef MPI
  while(1) {
    int nneighbours = 0;
    int nparticles_recv[NNEIGHBOURS];
    MPI_Request recv_req[NNEIGHBOURS];
    MPI_Request send_req[NNEIGHBOURS];
    for(int ii = 0; ii < NNEIGHBOURS; ++ii) {
      // Initialise received particles
      nparticles_recv[ii] = 0;

      // No communication required at edge
      if(neighbours[ii] == EDGE) {
        continue;
      }

      // Check which neighbours are sending some particles
      MPI_Irecv(
          &nparticles_recv[ii], 1, MPI_INT, neighbours[ii],
          TAG_SEND_RECV, MPI_COMM_WORLD, &recv_req[nneighbours]);
      MPI_Isend(
          &nparticles_sent[ii], 1, MPI_INT, neighbours[ii],
          TAG_SEND_RECV, MPI_COMM_WORLD, &send_req[nneighbours++]);
    }

    MPI_Waitall(
        nneighbours, recv_req, MPI_STATUSES_IGNORE);
    nneighbours = 0;

    // Manage all of the received particles
    int nunprocessed_particles = 0;
    const int unprocessed_start = nparticles;
    for(int ii = 0; ii < NNEIGHBOURS; ++ii) {
      if(neighbours[ii] == EDGE) {
        continue;
      }

      // Receive the particles from this neighbour
      for(int jj = 0; jj < nparticles_recv[ii]; ++jj) {
        MPI_Recv(
            &particles[unprocessed_start+nunprocessed_particles], 
            1, particle_type, neighbours[ii], TAG_PARTICLE, 
            MPI_COMM_WORLD, MPI_STATUSES_IGNORE);
        nunprocessed_particles++;
      }

      nparticles_recv[ii] = 0;
      nparticles_sent[ii] = 0;
    }

    nparticles += nunprocessed_particles;
    if(nunprocessed_particles) {
      handle_particles(
          global_nx, global_ny, nx, ny, x_off, y_off, dt, neighbours,
          density, edgex, edgey, &facets, &collisions, nparticles_sent,
          nunprocessed_particles, &nparticles, &particles[unprocessed_start],
          out_particles, cs_scatter_table, cs_absorb_table, energy_tally);
    }

    // Check if any of the ranks had unprocessed particles
    int particles_to_process;
    MPI_Allreduce(
        &nunprocessed_particles, &particles_to_process, 
        1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    // All ranks have reached census
    if(!particles_to_process) {
      break;
    }
  }
#endif

  barrier();

  *nlocal_particles = nparticles;

  printf("facets %d collisions %d\n", facets, collisions);
}

// Handles the current active batch of particles
void handle_particles(
    const int global_nx, const int global_ny, const int nx, const int ny, 
    const int x_off, const int y_off, const double dt, const int* neighbours, 
    const double* density, const double* edgex, const double* edgey, int* facets, 
    int* collisions, int* nparticles_sent, const int nunprocessed_particles,
    int* nparticles, Particle* particles_start, Particle* out_particles,
    CrossSection* cs_scatter_table, CrossSection* cs_absorb_table,
    double* energy_tally)
{
  int nout_particles = 0;
  int nlocal_particles = nunprocessed_particles;

  // Start by handling all of the initial local particles
  //#pragma omp parallel for reduction(+: facets, collisions)
  for(int pp = 0; pp < nunprocessed_particles; ++pp) {
    // Fetch the particle, correcting if we have performed replacements
    nout_particles = nunprocessed_particles-nlocal_particles;
    const int particle_index = pp-nout_particles;
    Particle* particle = &particles_start[particle_index];

    if(particle->dead) {
      printf("particle %d is dead \n", particle_index);
      continue;
    }

    handle_particle(
        global_nx, global_ny, nx, ny, x_off, y_off, dt, neighbours,
        particles_start, &nlocal_particles, nparticles_sent, facets, collisions,
        particle, density, edgex, edgey, &out_particles[nout_particles],
        cs_scatter_table, cs_absorb_table, energy_tally);
  }

  // Correct the new total number of particle
  *nparticles -= nout_particles;
  printf("handled %d particles\n", nunprocessed_particles);
}

// Handles an individual particle.
void handle_particle(
    const int global_nx, const int global_ny, const int nx, const int ny, 
    const int x_off, const int y_off, const double dt, const int* neighbours, 
    Particle* particles, int* nparticles, int* nparticles_sent, int* facets, 
    int* collisions, Particle* particle, const double* density, 
    const double* edgex, const double* edgey, Particle* out_particle,
    CrossSection* cs_scatter_table, CrossSection* cs_absorb_table,
    double* energy_tally)
{
  // (1) particle can stream and reach census
  // (2) particle can collide and either
  //      - the particle will be absorbed
  //      - the particle will scatter (this presumably means the energy changes)
  // (3) particle hits a boundary region and needs transferring to another process

  int x_facet = 0;
  double cell_mfp = 0.0;

  // Update the cross sections, referencing into the padded mesh
  int cellx = (particle->cell%global_nx)-x_off+PAD;
  int celly = (particle->cell/global_nx)-y_off+PAD;
  double local_density = density[celly*(nx+2*PAD)+cellx];
  double cs_scatter = total_cs_for_energy(cs_scatter_table, particle->e, local_density);
  double cs_absorb = total_cs_for_energy(cs_absorb_table, particle->e, local_density);
  double particle_velocity = sqrt((2.0*particle->e*eV)/PARTICLE_MASS);

  // Determine the number of mean free paths until the next collision
  particle->mfp_to_collision = -log(genrand())/cs_scatter;

  // Loop until we have reached census
  while(particle->dt_to_census > 0.0) {

    const double total_cross_section = cs_scatter+cs_absorb;
    cell_mfp = 1.0/total_cross_section;

    // Work out the distance until the particle hits a facet
    double distance_to_facet = 0.0;
    calc_distance_to_facet(
        global_nx, particle->x, particle->y, x_off, y_off, particle->omega_x,
        particle->omega_y, particle_velocity, particle->cell,
        &distance_to_facet, &x_facet, edgex, edgey);

    const double distance_to_collision = particle->mfp_to_collision*cell_mfp;
    const double distance_to_census = particle_velocity*particle->dt_to_census;

#if 0
    printf("%.12e %.12e %.12e\n", 
        distance_to_collision, distance_to_facet, distance_to_census);
#endif // if 0

    // Check if our next event is a collision
    if(distance_to_collision < distance_to_facet &&
        distance_to_collision < distance_to_census) {

      START_PROFILING(&compute_profile);

      // The cross sections for scattering and absorbtion were calculated on 
      // a previous iteration for our given energy
      handle_collision(
          particle, global_nx, nx, x_off, y_off, cs_absorb, total_cross_section, 
          distance_to_collision, energy_tally);

      // Energy has changed so update the cross-sections
      cs_scatter = total_cs_for_energy(cs_scatter_table, particle->e, local_density);
      cs_absorb = total_cs_for_energy(cs_absorb_table, particle->e, local_density);

      // Resample number of mean free paths to collision
      particle->mfp_to_collision = -log(genrand())/cs_scatter;
      particle->dt_to_census -= distance_to_collision/particle_velocity;
      particle_velocity = sqrt((2.0*particle->e*eV)/PARTICLE_MASS);

      (*collisions)++;

      STOP_PROFILING(&compute_profile, "collision");
    }
    // Check if we have reached facet
    else if(distance_to_facet < distance_to_census) {
      START_PROFILING(&compute_profile);

      // Check if we hit a facet, and jump out if particle left this rank's domain
      if(handle_facet_encounter(
            global_nx, global_ny, nx, ny, x_off, y_off, neighbours, 
            distance_to_facet, x_facet, nparticles, nparticles_sent, particle, 
            particles, out_particle)) {
        break;
      }

      // Update the local density and cross-sections
      cellx = (particle->cell%global_nx)-x_off+PAD;
      celly = (particle->cell/global_nx)-y_off+PAD;

      // Check if we need to update the density and cross sections
      if(local_density != density[celly*(nx+2*PAD)+cellx]) {
        local_density = density[celly*(nx+2*PAD)+cellx];
        cs_scatter = total_cs_for_energy(cs_scatter_table, particle->e, local_density);
        cs_absorb = total_cs_for_energy(cs_absorb_table, particle->e, local_density);
      }

      // Update the mean free paths until collision
      particle->mfp_to_collision -= (distance_to_facet/cell_mfp);
      particle->dt_to_census -= distance_to_facet/particle_velocity;

      (*facets)++;

      STOP_PROFILING(&compute_profile, "facet");
    }
    // Check if we have reached census
    else {
      // We have not changed cell or energy level at this stage
      particle->x += distance_to_census*particle->omega_x;
      particle->y += distance_to_census*particle->omega_y;
      particle->mfp_to_collision -= (distance_to_census/cell_mfp);
      particle->dt_to_census = 0.0;
    }
  }
}

// Makes the necessary updates to the particle given that
// the facet was encountered
int handle_facet_encounter(
    const int global_nx, const int global_ny, const int nx, const int ny, 
    const int x_off, const int y_off, const int* neighbours, 
    const double distance_to_facet, int x_facet, int* nparticles, 
    int* nparticles_sent, Particle* particle, Particle* particles, 
    Particle* out_particle)
{
  // We don't need to consider the halo regions in this package
  int cellx = particle->cell%global_nx;
  int celly = particle->cell/global_nx;

  // TODO: Make sure that the roundoff is handled here, perhaps actually set it
  // fully to one of the edges here
  particle->x += distance_to_facet*particle->omega_x;
  particle->y += distance_to_facet*particle->omega_y;

  // This use of x_facet is a slight misnoma, as it is really a facet
  // along the y dimensions
  if(x_facet) {
    if(particle->omega_x > 0.0) {
      // Reflect at the boundary
      if(cellx >= (global_nx-1)) {
        particle->omega_x = -(particle->omega_x);
      }
      else {
        // Definitely moving to right cell
        cellx += 1;
        particle->cell = celly*global_nx+cellx;

        // Check if we need to pass to another process
        if(cellx >= nx+x_off) {
          send_and_replace_particle(
              nparticles, neighbours[EAST], particles, particle, out_particle);
          nparticles_sent[EAST]++;
          return TRUE;
        }
      }
    }
    else if(particle->omega_x < 0.0) {
      if(cellx <= 0) {
        // Reflect at the boundary
        particle->omega_x = -(particle->omega_x);
      }
      else {
        // Definitely moving to left cell
        cellx -= 1;
        particle->cell = celly*global_nx+cellx;

        // Check if we need to pass to another process
        if(cellx < x_off) {
          send_and_replace_particle(
              nparticles, neighbours[WEST], particles, particle, out_particle);
          nparticles_sent[WEST]++;
          return TRUE;
        }
      }
    }
  }
  else {
    if(particle->omega_y > 0.0) {
      // Reflect at the boundary
      if(celly >= (global_ny-1)) {
        particle->omega_y = -(particle->omega_y);
      }
      else {
        // Definitely moving to north cell
        celly += 1;
        particle->cell = celly*global_nx+cellx;

        // Check if we need to pass to another process
        if(celly >= ny+y_off) {
          send_and_replace_particle(
              nparticles, neighbours[NORTH], particles, particle, out_particle);
          nparticles_sent[NORTH]++;
          return TRUE;
        }
      }
    }
    else if(particle->omega_y < 0.0) {
      // Reflect at the boundary
      if(celly <= 0) {
        particle->omega_y = -(particle->omega_y);
      }
      else {
        // Definitely moving to south cell
        celly -= 1;
        particle->cell = celly*global_nx+cellx;

        // Check if we need to pass to another process
        if(celly < y_off) {
          send_and_replace_particle(
              nparticles, neighbours[SOUTH], particles, particle, out_particle);
          nparticles_sent[SOUTH]++;
          return TRUE;
        }
      }
    }
  }

  return FALSE;
}

// Sends a particle to a neighbour and replaces in the particle list
void send_and_replace_particle(
    int* nparticles, const int destination, Particle* particles,
    Particle* particle_to_replace, Particle* out_particle)
{
#ifdef MPI
  if(destination == EDGE)
    return;

  // Reduce the number of particles by one
  (*nparticles)--;

  // Swap the current particle with the out particle
  *out_particle = *particle_to_replace;
  *particle_to_replace = particles[*nparticles];

  // Send the particle
  MPI_Send(
      out_particle, 1, particle_type, destination, TAG_PARTICLE, MPI_COMM_WORLD);

#else
#error "Unreachable - shouldn't send particles unless MPI enabled"
#endif
}

// Handle the collision event, including absorption and scattering
void handle_collision(
    Particle* particle, const int global_nx, const int nx, const int x_off, 
    const int y_off, const double cs_absorb, const double cs_total, 
    const double distance_to_collision, double* energy_tally)
{
  // Moves the particle to the collision site
  particle->x += distance_to_collision*particle->omega_x;
  particle->y += distance_to_collision*particle->omega_y;

  double de;
  const double p_absorb = cs_absorb/cs_total;
  if(genrand() < p_absorb) {

    /* Model particle absorption */

    // Find the new particle weight after absorption, saving the energy change
    const double new_weight = particle->weight*(1.0 - p_absorb);
    de = particle->e*(particle->weight-new_weight); 
    particle->weight = new_weight;

    // If the particle falls below the energy of interest then we will consider
    // it dead and it will be garbage collected at some point
    if(particle->e < MIN_ENERGY_OF_INTEREST) {
      //if(particle->weight*particle->e < MIN_ENERGY_OF_INTEREST)
      particle->dead = 1;
    }
  }
  else {

    /* Model particle scattering */

    // Choose a random scattering angle between -1 and 1
    const double mu_cm = 1.0 - 2.0*genrand();

    // Calculate the new energy based on the relation to angle of incidence
    // TODO: Could check here for the particular nuclide that was collided with
    const double e_new = particle->e*
      (MASS_NO*MASS_NO + 2.0*MASS_NO*mu_cm + 1.0)/
      ((MASS_NO + 1.0)*(MASS_NO + 1.0));

    // The change in energy experienced by the particle
    de = e_new-particle->e;

    // Set the new particle energy and location
    particle->e = e_new;

    // Convert the angle into the laboratory frame of reference
    double cos_theta =
      0.5*((MASS_NO+1.0)*sqrt(e_new/particle->e) - 
          (MASS_NO-1.0)*sqrt(particle->e/e_new));

    // Alter the direction of the velocities
    const double sin_theta = sin(acos(cos_theta));
    const double omega_x_new =
      (particle->omega_x*cos_theta - particle->omega_y*sin_theta);
    const double omega_y_new =
      (particle->omega_x*sin_theta + particle->omega_y*cos_theta);
    particle->omega_x = omega_x_new;
    particle->omega_y = omega_y_new;
  }

  // Remove the energy delta from the cell
  const int cellx = (particle->cell%global_nx)-x_off;
  const int celly = (particle->cell/global_nx)-y_off;
  energy_tally[celly*nx+cellx] -= de; 
}

// Calculate the distance to the next facet
void calc_distance_to_facet(
    const int global_nx, const double x, const double y, const int x_off,
    const int y_off, const double omega_x, const double omega_y,
    const double particle_velocity, const int cell, double* distance_to_facet,
    int* x_facet, const double* edgex, const double* edgey)
{
  // Check the timestep required to move the particle along a single axis
  // If the velocity is positive then the top or right boundary will be hit
  const int cellx = (cell%global_nx)-x_off+PAD;
  const int celly = (cell/global_nx)-y_off+PAD;
  double u_x_inv = 1.0/(omega_x*particle_velocity);
  double u_y_inv = 1.0/(omega_y*particle_velocity);

  // The bound is open on the left and bottom so we have to correct for this and
  // required the movement to the facet to go slightly further than the edge
  // in the calculated values, using OPEN_BOUND_CORRECTION, which is the smallest
  // possible distance we can be from the closed bound e.g. 1.0e-14.
  double dt_x = (omega_x > 0.0)
    ? (edgex[cellx+1]-x)*u_x_inv
    : ((edgex[cellx]-OPEN_BOUND_CORRECTION)-x)*u_x_inv;
  double dt_y = (omega_y > 0.0)
    ? (edgey[celly+1]-y)*u_y_inv
    : ((edgey[celly]-OPEN_BOUND_CORRECTION)-y)*u_y_inv;
  *x_facet = (dt_x < dt_y) ? 1 : 0;

  // Calculated the projection to be
  // a = vector on first edge to be hit
  // u = velocity vector

  double mag_u0 = particle_velocity;

  if(*x_facet) {
    // cos(theta) = ||(x, 0)||/||(u_x', u_y')|| - u' is u at boundary
    // cos(theta) = (x.u)/(||x||.||u||)
    // x_x/||u'|| = (x_x, 0)*(u_x, u_y) / (x_x.||u||)
    // x_x/||u'|| = (x_x.u_x / x_x.||u||)
    // x_x/||u'|| = u_x/||u||
    // ||u'|| = (x_x.||u||)/u_x
    // We are centered on the origin, so the y component is 0 after travelling
    // aint the x axis to the edge (ax, 0).(x, y)
    *distance_to_facet = (omega_x > 0.0)
      ? (edgex[cellx+1]-x)*mag_u0*u_x_inv
      : ((edgex[cellx]-OPEN_BOUND_CORRECTION)-x)*mag_u0*u_x_inv;
  }
  else {
    // We are centered on the origin, so the x component is 0 after travelling
    // aint the y axis to the edge (0, ay).(x, y)
    *distance_to_facet = (omega_y > 0.0)
      ? (edgey[celly+1]-y)*mag_u0*u_y_inv
      : ((edgey[celly]-OPEN_BOUND_CORRECTION)-y)*mag_u0*u_y_inv;
  }
}

// Fetch the cross section for a particular energy value
double total_cs_for_energy(
    const CrossSection* cs, const double energy, const double local_density)
{
  // Use a simple binary search
  int ind = cs->nentries/2;
  int width = ind/2;
  while(cs->key[ind-1] > energy || cs->key[ind] < energy) {
    width = max(1, width/2); // To handle odd cases, allows one extra walk
    ind += (cs->key[ind] > energy) ? -width : width;
  }

  // TODO: perform some interesting interpolation here
  // Center weighted is poor accuracy but might even out over enough particles
  const double microscopic_cs = (cs->value[ind-1] + cs->value[ind])/2.0;

  // This makes some assumption about the units of the data stored globally.
  // Might be worth making this more explicit somewhere.
  const double macroscopic_cs = 
    (local_density*AVOGADROS/MOLAR_MASS)*(microscopic_cs*BARNS);

  return macroscopic_cs;
}


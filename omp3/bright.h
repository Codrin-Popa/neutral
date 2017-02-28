#include "../bright_interface.h"

enum { PARTICLE_SENT, PARTICLE_DEAD, PARTICLE_CENSUS };

// Handles the current active batch of particles
void handle_particles(
    const int global_nx, const int global_ny, const int nx, const int ny, 
    const int x_off, const int y_off, const double dt, const int* neighbours, 
    const double* density, const double* edgex, const double* edgey, int* facets, 
    int* collisions, int* nparticles_sent, uint64_t* master_key, 
    const int ntotal_particles, const int nparticles_to_process, 
    int* nparticles, Particle* particles_start, CrossSection* cs_scatter_table, 
    CrossSection* cs_absorb_table, double* scalar_flux_tally, 
    double* energy_deposition_tally, RNPool* rn_pools);

// Tallies both the scalar flux and energy deposition in the cell
void update_tallies(
    const int nx, const int x_off, const int y_off, Particle* particle, 
    const double inv_ntotal_particles, const double energy_deposition,
    const double scalar_flux, double* scalar_flux_tally, 
    double* energy_deposition_tally);

// Makes the necessary updates to the particle given that
// the facet was encountered
int handle_facet_encounter(
    const int global_nx, const int global_ny, const int nx, const int ny, 
    const int x_off, const int y_off, const int* neighbours, 
    const double distance_to_facet, int x_facet, int* nparticles_sent, 
    Particle* particle);

// Sends a particle to a neighbour and replaces in the particle list
void send_and_mark_particle(
    const int destination, Particle* particle);

// Calculate the energy deposition in the cell
double calculate_energy_deposition(
    Particle* particle, const double path_length, const double number_density, 
    const double microscopic_cs_absorb, const double microscopic_cs_total);

// Fetch the cross section for a particular energy value
double microscopic_cs_for_energy(
    const CrossSection* cs, const double energy, int* cs_index);

// Validates the results of the simulation
void validate(
    const int nx, const int ny, const char* params_filename, 
    const int rank, double* energy_deposition_tally);


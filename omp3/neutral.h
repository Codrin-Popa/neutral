#include "../neutral_interface.h"

// Handles the current active batch of particles
void handle_particles(const int global_nx, const int global_ny, const int nx,
                      const int ny, const int pad, const int x_off,
                      const int y_off, const int initial, const double dt,
                      const double* density, const double* edgex,
                      const double* edgey, uint64_t* facets,
                      uint64_t* collisions, int* nparticles_sent,
                      uint64_t* master_key, const int ntotal_particles,
                      const int nparticles_to_process,
                      Particle* particles_start, CrossSection* cs_scatter_table,
                      CrossSection* cs_absorb_table,
                      double* energy_deposition_tally);

// Handles an individual particle.
int handle_particle(const int global_nx, const int global_ny, const int nx,
                    const int ny, const int pad, const int x_off,
                    const int y_off, const double dt, const int initial,
                    const int ntotal_particles, const double* density,
                    const double* edgex, const double* edgey,
                    const CrossSection* cs_scatter_table,
                    const CrossSection* cs_absorb_table, int* nparticles_sent,
                    uint64_t* facets, uint64_t* collisions, Particle* particle,
                    double* energy_deposition_tally, const uint64_t master_key);

// Tallies the energy deposition in the cell
void update_tallies(const int nx, const int x_off, const int y_off,
                    Particle* particle, const double inv_ntotal_particles,
                    const double energy_deposition,
                    double* energy_deposition_tally);

// Handle the collision event, including absorption and scattering
int handle_collision(Particle* particle, const double macroscopic_cs_absorb,
                     uint64_t* counter, const double macroscopic_cs_total,
                     const double distance_to_collision, uint64_t master_key);

// Makes the necessary updates to the particle given that
// the facet was encountered
int handle_facet_encounter(const int global_nx, const int global_ny,
                           const int nx, const int ny, const int x_off,
                           const int y_off, const double distance_to_facet,
                           int x_facet, int* nparticles_sent,
                           Particle* particle);

// Sends a particle to a neighbour and replaces in the particle list
void send_and_mark_particle(const int destination, Particle* particle);

// Calculate the distance to the next facet
void calc_distance_to_facet(const double x, const double y, const int pad,
                            const int x_off, const int y_off,
                            const double omega_x, const double omega_y,
                            const double speed, const int particle_cellx,
                            const int particle_celly, double* distance_to_facet,
                            int* x_facet, const double* edgex,
                            const double* edgey);

// Calculate the energy deposition in the cell
double calculate_energy_deposition(Particle* particle, const double path_length,
                                   const double number_density,
                                   const double microscopic_cs_absorb,
                                   const double microscopic_cs_total);

// Fetch the cross section for a particular energy value
double microscopic_cs_for_energy(const CrossSection* cs, const double energy,
                                 int* cs_index);

// Generates a pair of random numbers
void generate_random_numbers(const uint64_t master_key,
                             const uint64_t secondary_key, const uint64_t gid,
                             double* rn0, double* rn1);

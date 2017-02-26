#include <iostream> // for standard output
#include <iomanip> // for io manipulation (e.g. setw)
#include <random> // for randomness

#include "methods.h"

using namespace std;

// greatest common divisor
int gcd(const int a, const int b) {
  if (b == 0) return a;
  else return gcd(b, a % b);
}

// distance between two states
int distance(const vector<bool>& s1, const vector<bool>& s2) {
  int distance = 0;
  for (int ii = 0, size = s1.size(); ii < size; ii++) {
    distance += (s1[ii] == s2[ii]);
  }
  return distance;
}

// generate random state
vector<bool> random_state(const int nodes, uniform_real_distribution<double>& rnd,
                          mt19937_64& generator) {
  vector<bool> state(nodes);
  for (int ii = 0; ii < nodes; ii++) {
    state[ii] = (rnd(generator) < 0.5);
  }
  return state;
}

// make a random change to a given state using a random number on [0,1)
vector<bool> random_change(const vector<bool>& state, const double random) {
  const int node = floor(random*state.size());
  vector<bool> new_state = state;
  new_state[node] = !new_state[node];
  return new_state;
}

// hopfield network constructor
hopfield_network::hopfield_network(const vector<vector<bool>>& patterns) {
  nodes = patterns[0].size();

  // generate interaction matrix from patterns
  // note: these couplings are a factor of (nodes) greater than the regular definition
  couplings = vector<vector<int>>(nodes);
  max_energy = 0;
  for (int ii = 0; ii < nodes; ii++) {
    couplings[ii] = vector<int>(nodes, 0);
    for (int jj = 0; jj < nodes; jj++) {
      if (jj == ii) continue;
      for (int pp = 0; pp < int(patterns.size()); pp++) {
        const int coupling = (2*patterns[pp][ii]-1)*(2*patterns[pp][jj]-1);
        couplings[ii][jj] += coupling;
      }
      max_energy += abs(couplings[ii][jj]);
    }
  }

  max_energy_change = 0;
  energy_scale = max_energy;
  for (int ii = 0; ii < nodes; ii++) {
    int node_energy = 0;
    for (int jj = 0; jj < nodes; jj++) {
      node_energy += abs(couplings[ii][jj]);
    }
    max_energy_change = max(2*node_energy, max_energy_change);
    energy_scale = gcd(node_energy, energy_scale);
  }

  max_energy /= energy_scale;
  max_energy_change /= energy_scale;
  energy_range = 2*max_energy + 1;
};

// energy of the network in a given state
// note: this energy is shifted up by the maximum energy, and is an additional
//       factor of (nodes/energy_scale) greater than the regular definition
int hopfield_network::energy(const vector<bool>& state) const {
  int sum = 0;
  for (int ii = 0; ii < nodes; ii++) {
    for (int jj = ii + 1; jj < nodes; jj++) {
      sum += couplings[ii][jj] * (2*state[ii]-1) * (2*state[jj]-1);
    }
  }
  return -sum/energy_scale + max_energy;
}


void hopfield_network::print_couplings() const {
  int largest_coupling = 0;
  for (int ii = 0; ii < nodes; ii++) {
    for (int jj = 0; jj < nodes; jj++) {
      largest_coupling = max(abs(couplings[ii][jj]), largest_coupling);
    }
  }

  const int width = log10(largest_coupling) + 2;
  for (int ii = 0; ii < nodes; ii++) {
    for (int jj = 0; jj < nodes; jj++) {
      cout << setw(width) << couplings[ii][jj] << " ";
    }
    cout << endl;
  }
}

// network simulation constructor
network_simulation::network_simulation(const vector<vector<bool>>& patterns,
                                       const vector<bool>& initial_state) :
  patterns(patterns),
  network(hopfield_network(patterns))
{
  entropy_peak = network.max_energy;
  state = initial_state;
  initialize_histograms();
  visited = vector<bool>(network.energy_range, false);
  ln_weights = vector<double>(network.energy_range, 1);
  ln_dos = vector<double>(network.energy_range, 0);
};

// ---------------------------------------------------------------------------------------
// Access methods for histograms and matrices
// ---------------------------------------------------------------------------------------

// number of transitions from a given energy with a specified energy change
int network_simulation::transitions(const int energy, const int energy_change) const {
  return energy_transitions[energy][energy_change + network.max_energy_change];
}

// number of transitions from a given energy to any other energy
int network_simulation::transitions_from(const int energy) const {
  int sum = 0;
  for (int de = -network.max_energy_change; de <= network.max_energy_change; de++) {
    sum += transitions(energy, de);
  }
  return sum;
}

// actual transition matrix
double network_simulation::transition_matrix(const int final_energy,
                                             const int initial_energy) const {
  const int energy_change = final_energy - initial_energy;
  if (abs(energy_change) > network.max_energy_change) return 0;

  const int normalization = transitions_from(initial_energy);
  if (normalization == 0) return 0;

  return (double(transitions(initial_energy, energy_change))
          / transitions_from(initial_energy));
}

// ---------------------------------------------------------------------------------------
// Methods used in simulation
// ---------------------------------------------------------------------------------------

// reset all histograms and the visit log of visited energies
void network_simulation::initialize_histograms() {
  samples = vector<int>(network.energy_range, 0);
  energy_histogram = vector<long int>(network.energy_range, 0);
  state_histograms = vector<vector<long int>>(network.energy_range);
  energy_transitions = vector<vector<long int>>(network.energy_range);
  for (int ee = 0; ee < network.energy_range; ee++) {
    state_histograms[ee] = vector<long int>(network.nodes, 0);
    energy_transitions[ee] = vector<long int>(2*network.max_energy_change + 1, 0);
  }
}

// update histograms with an observation of a given energy
void network_simulation::update_energy_histogram(const int energy) {
  energy_histogram[energy]++;
}
void network_simulation::update_state_histograms(const int energy) {
  for (int ii = 0; ii < network.nodes; ii++) {
    state_histograms[energy][ii] += state[ii];
  }
}

// update sample count
void network_simulation::update_samples(const int new_energy, const int old_energy) {
  if (!visited[new_energy]) {
    visited[new_energy] = true;
    samples[new_energy]++;
  }

  if (old_energy == entropy_peak) return;
  if (new_energy == entropy_peak) {
    visited = vector<bool>(network.energy_range, false);
    return;
  }

  const bool above_peak_now = (new_energy > entropy_peak);
  const bool above_peak_before = (old_energy > entropy_peak);
  if (above_peak_now == above_peak_before) return;

  if (above_peak_now) {
    // reset visit log at low energies
    for (int ee = 0; ee < entropy_peak; ee++) {
      visited[ee] = false;
    }
  } else {
    // reset visit log at high energies
    for (int ee = entropy_peak + 1; ee < network.energy_range; ee++) {
      visited[ee] = false;
    }
  }
}

// expectation value of fractional sample error at a given temperature
// WARNING: assumes that the density of states is up to date
double network_simulation::fractional_sample_error(const double temp) const {
  double error = 0;
  double normalization = 0;
  for (int ee = 0; ee < network.energy_range; ee++) {
    if (samples[ee] != 0) {
      const double boltzmann_factor = exp(ln_dos[ee] - ee/temp);
      error += boltzmann_factor/sqrt(samples[ee]);
      normalization += boltzmann_factor;
    }
  }
  return error/normalization;
}

// add to transition matrix
void network_simulation::add_transition(const int energy, const int energy_change) {
  energy_transitions[energy][energy_change + network.max_energy_change]++;
}

// compute density of states and weight array from transition matrix
void network_simulation::compute_dos_and_weights_from_transitions(const double temp_cap) {

  ln_dos = vector<double>(network.energy_range, 0);
  ln_weights = vector<double>(network.energy_range, 1);

  double max_ln_dos = 0;

  // sweep across energies to construct the density of states
  for (int ee = 1; ee < network.energy_range; ee++) {

    ln_dos[ee] = ln_dos[ee-1];

    if (energy_histogram[ee] == 0) continue;

    double flux_up_to_this_energy = 0;
    double flux_down_from_this_energy = 0;
    for (int smaller_ee = 0; smaller_ee < ee; smaller_ee++) {
      flux_up_to_this_energy += (exp(ln_dos[smaller_ee] - ln_dos[ee])
                                 * transition_matrix(ee, smaller_ee));
      flux_down_from_this_energy += transition_matrix(smaller_ee, ee);
    }
    if (flux_up_to_this_energy > 0 && flux_down_from_this_energy > 0) {
      ln_dos[ee] += log(flux_up_to_this_energy/flux_down_from_this_energy);
    }

    if (ln_dos[ee] > max_ln_dos) {
      max_ln_dos = ln_dos[ee];
      entropy_peak = ee;
    }

  }

  if (temp_cap > 0) {

    int smallest_seen_energy = 0;
    for (int ee = 0; ee < network.energy_range; ee++) {
      if (energy_histogram[ee] != 0) {
        smallest_seen_energy = ee;
        break;
      }
    }

    // in the relevant range of observed energies, set weights appropriately
    for (int ee = entropy_peak; ee > smallest_seen_energy; ee--) {
      ln_weights[ee] = -ln_dos[ee];
    }
    // below all observed energies use weights fixed at the temperature cap
    for (int ee = smallest_seen_energy; ee >= 0; ee--) {
      ln_weights[ee] = (-ln_dos[smallest_seen_energy]
                        - abs(smallest_seen_energy - ee) / temp_cap);
    }
    // above the entropy peak, use flat (infinite temperature) weights
    for (int ee = network.energy_range - 1; ee > entropy_peak; ee--) {
      ln_weights[ee] = -ln_dos[entropy_peak];
    }

  } else { // if temp_cap < 0

    int largest_seen_energy = network.energy_range;
    for (int ee = network.energy_range - 1; ee >= 0; ee++) {
      if (energy_histogram[ee] != 0) {
        largest_seen_energy = ee;
        break;
      }
    }

    // in the relevant range of observed energies, set weights appropriately
    for (int ee = entropy_peak; ee < largest_seen_energy; ee++) {
      ln_weights[ee] = -ln_dos[ee];
    }
    // above all observed energies use weights fixed at the temperature cap
    for (int ee = largest_seen_energy; ee < network.energy_range; ee++) {
      ln_weights[ee] = (-ln_dos[largest_seen_energy]
                        - abs(largest_seen_energy - ee) / temp_cap);
    }
    // below the entropy peak, use flat (infinite temperature) weights
    for (int ee = 0; ee < entropy_peak; ee++) {
      ln_weights[ee] = -ln_dos[entropy_peak];
    }

  }

}

// compute density of states from the energy histogram
void network_simulation::compute_dos_from_energy_histogram() {
  ln_dos = vector<double>(network.energy_range);

  double max_ln_dos = 0;
  for (int ee = 0; ee < network.energy_range; ee++) {
    ln_dos[ee] = log(energy_histogram[ee]) - ln_weights[ee];
    max_ln_dos = max(ln_dos[ee], max_ln_dos);
  }
  for (int ee = 0; ee < network.energy_range; ee++) {
    ln_dos[ee] -= max_ln_dos;
  }
};

// ---------------------------------------------------------------------------------------
// Printing methods
// ---------------------------------------------------------------------------------------

// print simulation patterns
void network_simulation::print_patterns() const {
  for (int ii = 0; ii < int(patterns.size()); ii++) {
    for (int jj = 0; jj < network.nodes; jj++) {
      cout << patterns[ii][jj] << " ";
    }
    cout << endl;
  }
}

// print energy histogram, density of states, and energy samples
void network_simulation::print_energy_data() const {
  cout << "energy observations samples ln_dos" << endl;
  const int energy_width = log10(2*network.max_energy) + 2;
  const int histogram_width = log10(energy_histogram[entropy_peak]) + 2;
  for (int ee = network.energy_range - 1; ee >= 0; ee--) {
    const int observations = energy_histogram[ee];
    if (observations != 0) {
      cout << fixed
           << setw(energy_width) << ee - network.max_energy << " "
           << setw(histogram_width) << observations << " "
           << setw(histogram_width) << samples[ee] << " "
           << setw(10) << setprecision(7) << ln_dos[ee] << " "
           << endl;
    }
  }
}

// print expectation values of spins at each energy
void network_simulation::print_expected_states() const {
  cout << setprecision(7);
  for (int ee = network.energy_range - 1; ee >= 0; ee--) {
    const int observations = energy_histogram[ee];
    if (observations > 0) {
      cout << setw(log10(2*network.max_energy) + 2)
           << ee - network.max_energy << " ";
      for (int ii = 0; ii < network.nodes; ii++) {
        cout << setw(10) << 2*double(state_histograms[ee][ii])/observations - 1 << " ";
      }
      cout << endl;
    }
  }
}

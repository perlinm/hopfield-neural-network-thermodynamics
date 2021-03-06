#include <iostream> // for standard output
#include <iomanip> // for io manipulation (e.g. setw)
#include <random> // for randomness
#include <sstream> // for string streams
#include <algorithm> // for sort method
#include <fstream> // for stream objects

#include "methods.h"

using namespace std;

string time_string(const int total_seconds) {
  const int seconds = total_seconds % 60;
  const int minutes = (total_seconds / 60) % 60;
  const int hours = (total_seconds / (60 * 60)) % (24);
  const int days = total_seconds / (60 * 60 * 24);
  return (to_string(days) + "d "
          + to_string(hours) + "h "
          + to_string(minutes) + "m "
          + to_string(seconds) + "s");
}

// greatest common divisor
int gcd(const int a, const int b) {
  if (b == 0) return a;
  else return gcd(b, a % b);
}

// generate a random state
vector<bool> random_state(const int nodes, uniform_real_distribution<double>& rnd,
                          mt19937_64& generator) {
  vector<bool> state(nodes);
  for (int ii = 0; ii < nodes; ii++) {
    state[ii] = (rnd(generator) < 0.5);
  }
  return state;
}

// hopfield network constructor
hopfield_network::hopfield_network(const vector<vector<bool>>& patterns) {
  // number of nodes in network
  nodes = patterns[0].size();

  // generate interaction matrix from patterns
  // note: these couplings are a factor of (nodes) greater than the regular definition
  couplings = vector<vector<int>>(nodes);
  for (int ii = 0; ii < nodes; ii++) {
    couplings[ii] = vector<int>(nodes, 0);
    for (int jj = 0; jj < nodes; jj++) {
      if (jj == ii) continue;
      for (int pp = 0, size = patterns.size(); pp < size; pp++) {
        couplings[ii][jj] += 2 * (patterns[pp][ii] == patterns[pp][jj]) - 1;
      }
    }
  }

  // determine the maximum energy change possible in one move
  max_energy_change = 0;
  energy_scale = 0;
  for (int ii = 0; ii < nodes; ii++) {
    int node_energy = 0;
    int node_resolution = 0;
    for (int jj = 0; jj < nodes; jj++) {
      node_energy += 2*abs(couplings[ii][jj]);
      node_resolution = gcd(2*abs(couplings[ii][jj]), node_resolution);
    }
    max_energy_change = max(node_energy, max_energy_change);
    energy_scale = gcd(node_resolution, energy_scale);
  }

  // compute an upper bound on the maximum energy achievable by network
  // given that the actual energy is
  //   1/2 \sum_{i,j} J_{ij} s_i s_j with s_i, s_j in {-1, 1}
  //   an upper bound is 1/2 \sum_{i,j} |J_{ij}| = \sum_{i,j>i} |J_{ij}|
  max_energy = 0;
  for (int ii = 0; ii < nodes; ii++) {
    for (int jj = ii + 1; jj < nodes; jj++) {
      max_energy += abs(couplings[ii][jj]);
    }
  }
  // fix up max_energy so that for any energy E we observe,
  //   (E + max_energy) is divisible by energy_scale
  const int offset = (max_energy + energy(patterns[0])) % energy_scale;
  max_energy += (energy_scale - offset) % energy_scale;

};

// (index of) energy of the network in a given state
int hopfield_network::energy(const vector<bool>& state) const {
  int energy = 0;
  for (int ii = 0; ii < nodes; ii++) {
    const bool node_state = state[ii];
    for (int jj = ii + 1; jj < nodes; jj++) {
      energy -= couplings[ii][jj] * (2 * (node_state == state[jj]) - 1);
    }
  }
  return (energy + max_energy) / energy_scale;
}

// convert energy index to an "actual" energy
int hopfield_network::actual_energy(const int energy_index) const {
  return energy_index * energy_scale - max_energy;
}

// print coupling matrix
void hopfield_network::print_couplings() const {
  // determin the largest coupling constant, which tells us how wide to make
  // the columns of the matrix
  int largest_coupling = 0;
  for (int ii = 0; ii < nodes; ii++) {
    for (int jj = 0; jj < nodes; jj++) {
      largest_coupling = max(abs(couplings[ii][jj]), largest_coupling);
    }
  }
  // matrix column width
  const int width = log10(largest_coupling) + 2;

  // print the matrix
  cout << "coupling matrix:" << endl;
  for (int ii = 0; ii < nodes; ii++) {
    for (int jj = 0; jj < nodes; jj++) {
      cout << setw(width) << couplings[ii][jj] << " ";
    }
    cout << endl;
  }
}

// network simulation constructor
network_simulation::network_simulation(const vector<vector<bool>>& patterns,
                                       const vector<bool>& initial_state,
                                       const bool fixed_temp) :
  fixed_temp(fixed_temp),
  patterns(patterns),
  pattern_number(patterns.size()),
  network(hopfield_network(patterns)),
  energy_range(2*network.max_energy/network.energy_scale),
  max_de(network.max_energy_change/network.energy_scale)
{
  entropy_peak = energy_range / 2; // an initial guess
  state = initial_state;
  initialize_histograms();
  if (!fixed_temp) {
    ln_weights = vector<double>(energy_range, 0);
  }
};

// ---------------------------------------------------------------------------------------
// Access methods for histograms and matrices
// ---------------------------------------------------------------------------------------

// number of attempted transitions from a given energy with a specified energy change
long network_simulation::transitions(const int energy, const int energy_change) const {
  return transition_histogram[energy][energy_change + max_de];
}

// number of attempted transitions from a given energy into any other energy
long network_simulation::transitions_from(const int energy) const {
  long count = 0;
  for (long de = -max_de; de <= max_de; de++) {
    count += transitions(energy, de);
  }
  return count;
}

// elements of the actual normalized transition matrix:
//   the probability of moving from a given initial energy into a specific final energy
double network_simulation::transition_matrix(const int final_energy,
                                             const int initial_energy) const {
  const int energy_change = final_energy - initial_energy;
  if (abs(energy_change) > max_de) return 0;

  // normalization factor: sum of all transitions from the initial energy
  const long normalization = transitions_from(initial_energy);

  // if the normalization factor is zero, it's because we have never seen this energy
  // by default, set these elements of the transition energy to zero
  if (normalization == 0) return 0;

  return double(transitions(initial_energy, energy_change)) / normalization;
}

// ---------------------------------------------------------------------------------------
// Methods used in simulation
// ---------------------------------------------------------------------------------------

// compute energy change due to flipping a node from its current state
int network_simulation::node_flip_energy_change(const int node) const {
  const bool node_state = state[node];
  int node_energy = 0;
  for (int ii = 0; ii < network.nodes; ii++) {
    node_energy -= network.couplings[node][ii] * (2 * (node_state == state[ii]) - 1);
  }
  return - 2 * node_energy / network.energy_scale;
}

// probability to accept a move
double network_simulation::move_probability(const int current_energy,
                                            const int energy_change,
                                            const double temp) {
  if (!fixed_temp) {
    return exp(ln_weights[current_energy + energy_change] - ln_weights[current_energy]);
  } else {
    return exp(-energy_change/temp);
  }
}

// initialize all tables and histograms
void network_simulation::initialize_histograms() {
  energy_histogram = vector<long>(energy_range, 0);

  if (!fixed_temp) {
    ln_dos = vector<double>(energy_range, 0);
    visit_log = vector<bool>(energy_range, true);
    sample_histogram = vector<long>(energy_range, 0);
    all_temp_distance_records = vector<long>(energy_range, 0);
    all_temp_distance_logs = vector<long>(energy_range, 0);

    transition_histogram = vector<vector<long>>(energy_range);
    for (int ee = 0; ee < energy_range; ee++) {
      transition_histogram[ee] = vector<long>(2*max_de + 1, 0);
    }

  } else { // if fixed_temp
    state_histograms = vector<long>(network.nodes, 0);
  }
}

void network_simulation::update_distance_logs(const int energy) {
  int min_distance = network.nodes;
  // for each pattern pp
  for (int pp = 0; pp < pattern_number; pp++) {
    // find overlap between the current state and patterns[pp]
    int overlap = 0;
    for (int ii = 0; ii < network.nodes; ii++) {
      overlap += (state[ii] == patterns[pp][ii]);
    }
    min_distance = min({min_distance, overlap, network.nodes - overlap});
  }
  // add to distance logs
  if (!fixed_temp) {
    all_temp_distance_records[energy]++;
    all_temp_distance_logs[energy] += min_distance;
  } else {
    fixed_temp_distance_log += min_distance;
    fixed_temp_distance_records++;
  }
}

void network_simulation::update_state_histograms() {
  if (!fixed_temp) return;
  for (int ii = 0; ii < network.nodes; ii++) {
    state_histograms[ii] += state[ii];
  }
  state_records++;
}

void network_simulation::update_sample_histogram(const int new_energy,
                                                 const int old_energy) {
  if (fixed_temp) return;
  // if we have not yet visited this energy since the last observation
  //   of a maximual entropy state, add to the sample histogram
  if (!visit_log[new_energy]) {
    visit_log[new_energy] = true;
    sample_histogram[new_energy]++;
  }

  // if we are at the entropy peak, reset the visit log and return
  if (new_energy == entropy_peak) {
    if (old_energy != entropy_peak) {
      visit_log = vector<bool>(energy_range, false);
    } else {
      // if we were at the entropy peak the last time we updated the sample histogram,
      //   we only need to reset the visit log at the entropy peak itself,
      //   as it is already false everywhere else
      visit_log[entropy_peak] = false;
    }
    return;
  }

  // determine whether we have crossed the entropy peak since the last move
  const bool above_peak_now = (new_energy > entropy_peak);
  const bool above_peak_before = (old_energy > entropy_peak);

  // if we did not cross the entropy peak, return
  if (above_peak_now == above_peak_before) return;

  // if we did cross the entropy peak, reset the appropriate parts of the visit log
  if (above_peak_now) {
    // reset visit log below the entropy peak
    for (int ee = 0; ee < entropy_peak; ee++) {
      visit_log[ee] = false;
    }
  } else { // if below peak now
    // reset visit log above the entropy peak
    for (int ee = entropy_peak + 1; ee < energy_range; ee++) {
      visit_log[ee] = false;
    }
  }
}

void network_simulation::update_transition_histogram(const int energy,
                                                     const int energy_change) {
  transition_histogram[energy][energy_change + max_de]++;
}

// compute density of states from the transition matrix
void network_simulation::compute_dos_from_transitions() {

  // keep track of the maximal value of ln_dos
  double max_ln_dos = 0;

  // sweep up through all energies to "bootstrap" the density of states
  ln_dos[0] = 0; // seed a value of ln_dos for the sweep
  for (int ee = 1; ee < energy_range; ee++) {

    // pick an initial guess for the density of states at the energy ee
    ln_dos[ee] = ln_dos[ee-1];

    // if we haven't seen this energy enough times to get any real statistics on
    //   transitions from it, we don't have enough information to make any real
    //   corrections to the previous guess correct the previous guess, so we go on
    //   to the next energy
    if (energy_histogram[ee] < max_de) continue;

    // given our guess for the density of states at the energy ee,
    //   compute the net transition fluxes up to ee from below (i.e. from lower energies),
    //   and down from ee (i.e. to lower energies)
    double flux_up_to_this_energy = 0;
    double flux_down_from_this_energy = 0;
    for (int smaller_ee = ee - max_de; smaller_ee < ee; smaller_ee++) {
      if (smaller_ee < 0) continue;
      // we divide both normalized fluxes by the guess for the density of states at ee
      //   in order to avoid potential numerical overflows (and reduce numerical error)
      // as we will actually be interested in the ratio of these fluxes,
      //   multiplying them both by a constant factor has no consequence
      flux_up_to_this_energy += (exp(ln_dos[smaller_ee] - ln_dos[ee])
                                 * transition_matrix(ee, smaller_ee));
      flux_down_from_this_energy += transition_matrix(smaller_ee, ee);
    }

    // in an equilibrium ensemble of simulations, the two fluxes we computed above
    //   should be the same; if they are not, it is because our guess for
    //   the density of states was incorrect
    // we therefore multiply the density of states by the factor which would make
    //   these fluxes equal, which is presicely the ratio of the fluxes
    if (flux_up_to_this_energy > 0 && flux_down_from_this_energy > 0) {
      ln_dos[ee] += log(flux_up_to_this_energy/flux_down_from_this_energy);
    }

    // keep track of the maximum value of ln_dos,
    //  and the energy at which the density of states is maximal (i.e. the entropy peak)
    if (ln_dos[ee] > max_ln_dos) {
      max_ln_dos = ln_dos[ee];
      entropy_peak = ee;
    }

  }

  // subtract off the maximal value of ln_dos from the entire array,
  //   which normalizes the density of states to 1 at the entropy peak
  for (int ee = 0; ee < energy_range; ee++) {
    ln_dos[ee] -= max_ln_dos;
  }

}

// compute density of states from the energy histogram
void network_simulation::compute_dos_from_energy_histogram() {
  if (fixed_temp) return;
  // keep track of the maximal value of ln_dos
  double max_ln_dos = 0;
  for (int ee = 0; ee < energy_range; ee++) {
    ln_dos[ee] = log(energy_histogram[ee]) - ln_weights[ee];
    max_ln_dos = max(ln_dos[ee], max_ln_dos);
  }
  // subtract off the maximal value of ln_dos from the entire array,
  //   which normalizes the density of states to 1 at the entropy peak
  for (int ee = 0; ee < energy_range; ee++) {
    ln_dos[ee] -= max_ln_dos;
  }
};

// construct weight array from the density of states
// WARNING: assumes that the density of states is up to date
void network_simulation::compute_weights_from_dos(const double temp) {
  if (fixed_temp) return;
  // reset the weight array
  ln_weights = vector<double>(energy_range, 0);

  if (temp > 0) {
    // if we care about positive temperatures, then we are interested in low energies
    // identify the lowest energy we have seen
    int lowest_seen_energy = 0;
    for (int ee = 0; ee < energy_range; ee++) {
      if (energy_histogram[ee] != 0) {
        lowest_seen_energy = ee;
        break;
      }
    }

    // in the relevant range of observed energies, set weights appropriately,
    //   but never set any weight higher than we would at the simulation temperature
    double excess_weight = 0;
    const double max_diff = energy_range / temp;
    for (int ee = entropy_peak - 1; ee >= lowest_seen_energy; ee--) {
      ln_weights[ee] = -ln_dos[ee];
      const double diff = ln_weights[ee] - ln_weights[ee+1];
      if (diff > max_diff) {
        excess_weight += max_diff - diff;
        ln_weights[ee] -= excess_weight;
      }
    }
    // below all observed energies, use fixed temperature weights
    for (int ee = 0; ee < lowest_seen_energy; ee++) {
      ln_weights[ee] = (-ln_dos[lowest_seen_energy]
                        + (lowest_seen_energy - ee) / temp);
    }

  } else { // if temp < 0

    // if we care about negative temperatures, then we are interested in high energies
    // identify the highest energy we have seen
    int highest_seen_energy = energy_range;
    for (int ee = energy_range - 1; ee >= 0; ee--) {
      if (energy_histogram[ee] != 0) {
        highest_seen_energy = ee;
        break;
      }
    }

    // in the relevant range of observed energies, set weights appropriately
    //   but never set any weight higher than we would at the simulation temperature
    double excess_weight = 0;
    const double max_diff = - energy_range / temp;
    for (int ee = entropy_peak + 1; ee <= highest_seen_energy; ee++) {
      ln_weights[ee] = -ln_dos[ee];
      const double diff = ln_weights[ee] - ln_weights[ee-1];
      if (diff > max_diff) {
        excess_weight += max_diff - diff;
        ln_weights[ee] -= excess_weight;
      }
    }
    // above all observed energies, use fixed temperature weights
    for (int ee = highest_seen_energy + 1; ee < energy_range; ee++) {
      ln_weights[ee] = (-ln_dos[highest_seen_energy]
                        + (highest_seen_energy - ee) / temp);
    }
  }

}

// expectation value of fractional sample error at the simulation temperature
// WARNING: assumes that the density of states is up to date
double network_simulation::fractional_sample_error(const double temp) const {

  // determine the lowest and highest energies we care about
  int lowest_energy;
  int highest_energy;
  if (temp > 0) { // we care about low energies
    highest_energy = entropy_peak;
    // set lowest_energy to the lowest energy we have sampled
    for (int ee = 0; ee < entropy_peak; ee++) {
      if (sample_histogram[ee] != 0) {
        lowest_energy = ee;
        break;
      }
    }

  } else { // we care about low energies
    lowest_energy = entropy_peak;
    // set highest_energy to the highest energy we have sampled
    for (int ee = energy_range - 1; ee > entropy_peak; ee--) {
      if (sample_histogram[ee] != 0) {
        highest_energy = ee;
        break;
      }
    }
  }
  // the mean energy we care about
  const int mean_energy = (highest_energy + lowest_energy) / 2;

  // sum up the fractional error in sample counts with appropriate boltzmann factors
  long double error = 0;
  long double normalization = 0; // this is the partition function
  for (int ee = lowest_energy; ee < highest_energy; ee++) {
    if (sample_histogram[ee] != 0) {
      // offset ln_dos[ee] and the energy ee by their values at the mean energy
      //   we care about in order to avoid numerical overflows
      // this offset amounts to multiplying both (error) and (normalization) by
      //   a constant factor, which means that it does not affect (error/normalization)
      const long double ln_dos_ee = ln_dos[ee] - ln_dos[mean_energy];
      const long double energy = ee - mean_energy;
      const long double boltzmann_factor = expl(ln_dos_ee - energy / temp);
      error += boltzmann_factor/sqrt(sample_histogram[ee]);
      normalization += boltzmann_factor;
    }
  }
  if (error == 0) return 2;
  return error/normalization;
}

// ---------------------------------------------------------------------------------------
// Writing/reading data files
// ---------------------------------------------------------------------------------------

void network_simulation::write_transitions_file(const string transitions_file,
                                                const string file_header) const {
  ofstream transition_stream(transitions_file);
  transition_stream << file_header << endl
                    << "# (row)x(column) = (energy)x(de)" << endl;
  for (int ee = 0; ee < energy_range; ee++) {
    if (energy_histogram[ee] == 0) continue;
    transition_stream << network.actual_energy(ee)
                      << " " << transitions(ee, -max_de);
    for (int de = -max_de + 1; de <= max_de; de++) {
      transition_stream << " " << transitions(ee, de);
    }
    transition_stream << endl;
  }
  transition_stream.close();
}

void network_simulation::write_weights_file(const string weights_file,
                                            const string file_header) const {
  if (fixed_temp) return;
  ofstream weight_stream(weights_file);
  weight_stream << file_header << endl
                << "# energy, ln_weight" << endl;
  for (int ee = 0; ee < energy_range; ee++) {
    if (energy_histogram[ee] == 0) continue;
    weight_stream << setprecision(numeric_limits<double>::max_digits10)
                  << network.actual_energy(ee) << " "
                  << ln_weights[ee] << endl;
  }
  weight_stream.close();
}

void network_simulation::write_energy_file(const string energy_file,
                                           const string file_header) const {
  ofstream energy_stream(energy_file);
  energy_stream << file_header << endl
                << "# energy, energy histogram";
  if (!fixed_temp) energy_stream << ", sample_histogram";
  energy_stream << endl;
  for (int ee = 0; ee < energy_range; ee++) {
    if (energy_histogram[ee] == 0)  continue;
    energy_stream << network.actual_energy(ee) << " " << energy_histogram[ee];
    if (!fixed_temp) energy_stream << " " << sample_histogram[ee];
    energy_stream << endl;
  }
  energy_stream.close();
}

void network_simulation::write_distance_file(const string distance_file,
                                             const string file_header) const {
  ofstream distance_stream(distance_file);
  distance_stream << file_header << endl;
  if (!fixed_temp) {
    distance_stream << "# energy, records, distance log" << endl;
    for (int ee = 0; ee < energy_range; ee++) {
      if (all_temp_distance_records[ee] == 0)  continue;
      distance_stream << network.actual_energy(ee) << " "
                      << all_temp_distance_records[ee] << " "
                      << all_temp_distance_logs[ee] << endl;
    }
  } else {
    distance_stream << "# records, distance log " << endl
                    << fixed_temp_distance_records << " "
                    << fixed_temp_distance_log << endl;
  }
  distance_stream.close();
}

void network_simulation::write_state_file(const string state_file,
                                          const string file_header) const {
  if (!fixed_temp) return;
  ofstream state_stream(state_file);
  state_stream << file_header << endl
               << "# state records: " << state_records << endl
               << "# state histogram: " << endl;
  for (int ii = 0; ii < network.nodes; ii++) {
    state_stream << state_histograms[ii] << endl;
  }
  state_stream.close();
}

void network_simulation::read_transitions_file(const string transitions_file) {
  cout << "reading in transition matrix" << endl;
  ifstream input(transitions_file);
  string line;
  string word;
  while (getline(input,line)) {
    if (line[0] == '#' || line.empty()) continue;
    stringstream line_stream(line);
    line_stream >> word;
    const int ee = (stoi(word) + network.max_energy) / network.energy_scale;
    energy_histogram[ee]++; // mark this energy as seen
    for (int dd = 0; dd < 2*max_de + 1 ; dd++) {
      line_stream >> word;
      transition_histogram[ee][dd] = stoi(word);
    }
  }
}

void network_simulation::read_weights_file(const string weights_file) {
  if (fixed_temp) return;
  // keep track of first and last zeroes in ln_weights
  bool first_zero_set = false;
  int first_zero, last_zero;

  // keep track of lowest and highest energies seen
  bool lowest_seen_energy_set = false;
  int lowest_seen_energy, highest_seen_energy;

  // maximum temperature of interest specified in weights file
  double temp;

  cout << "reading in weight array" << endl;
  ifstream input(weights_file.c_str());
  string line;
  string word;
  while (getline(input,line)) {
    if (line.empty()) continue;
    stringstream line_stream(line);
    line_stream >> word;

    if (word == "#") {
      if (word.find("input_temp") != string::npos) {
        line_stream >> word;
        temp = stod(word) * network.nodes / network.energy_scale;
      }
      continue;
    }

    const int ee = (stoi(word) + network.max_energy) / network.energy_scale;
    energy_histogram[ee]++; // mark this energy as seen

    if (!lowest_seen_energy_set) {
      lowest_seen_energy_set = true;
      lowest_seen_energy = ee;
    }
    highest_seen_energy = ee;

    line_stream >> word;
    const double weight = stod(word);
    ln_weights[ee] = weight;

    if (weight == 0) {
      if (!first_zero_set) {
        first_zero_set = true;
        first_zero = ee;
      }
      last_zero = ee;
    }
  }

  // fill in the weight array within the range of seen energies
  for (int ee = lowest_seen_energy + 1; ee < highest_seen_energy; ee++) {
    if (ln_weights[ee] == 0) ln_weights[ee] = ln_weights[ee-1];
  }

  // set entropy peak and fill in the rest of the weight array
  if (temp > 0) {
    entropy_peak = first_zero;
    for (int ee = 0; ee < lowest_seen_energy; ee++) {
      ln_weights[ee] = (ln_weights[lowest_seen_energy]
                        + (lowest_seen_energy - ee) / temp);
    }

  } else { // if temp < 0
    entropy_peak = last_zero;
    for (int ee = highest_seen_energy + 1; ee < energy_range; ee++) {
      ln_weights[ee] = (ln_weights[highest_seen_energy]
                        + (highest_seen_energy - ee) / temp);
    }
  }

}

// ---------------------------------------------------------------------------------------
// Printing methods
// ---------------------------------------------------------------------------------------

// print patterns defining the simulated network
void network_simulation::print_patterns() const {
  const int energy_width = log10(network.max_energy) + 2;
  const int index_width = log10(pattern_number) + 1;

  // make list of the pattern energies
  vector<int> energies(pattern_number);
  for (int pp = 0; pp < pattern_number; pp++) {
    energies[pp] = energy(patterns[pp]);
  }

  // sort the pattern energies
  vector<int> sorted_energies = energies;
  sort(sorted_energies.begin(), sorted_energies.end());

  // printed[pp]: have we printed pattern pp?
  vector<bool> printed(pattern_number,false);

  // print patterns in order of decreasing energy
  cout << "(energy, index) pattern" << endl;
  for (int ss = pattern_number - 1; ss >= 0; ss--) {
    cout << "(" << setw(energy_width)
         << network.actual_energy(sorted_energies[ss]) << ", ";
    for (int pp = 0; pp < pattern_number; pp++) {
      if (energies[pp] == sorted_energies[ss] && !printed[pp]) {
        cout << setw(index_width) << pp << ")";
        for (int ii = 0; ii < network.nodes; ii++) {
          cout << " " << patterns[pp][ii];
        }
        printed[pp] = true;
        break;
      }
    }
    cout << endl;
  }
}

// for each energy observed, print the energy and the corresponding values in
//   the energy histogram, sample histogram, density of states, and weight
void network_simulation::print_energy_data() const {
  // get the maximum value in the energy histogram
  long most_observations = 0;
  for (int ee = 0; ee < energy_range; ee++) {
    most_observations = max(energy_histogram[ee], most_observations);
  }
  cout << "energy observations samples log10_dos ln10_weights" << endl;
  const int energy_width = log10(network.max_energy) + 2;
  const int energy_hist_width = log10(most_observations) + 1;
  const int sample_width = log10(sample_histogram[entropy_peak]) + 1;
  const int double_dec = 6; // decimal precision with which to print doubles
  for (int ee = energy_range - 1; ee >= 0; ee--) {
    const int observations = energy_histogram[ee];
    if (observations == 0) continue;
    cout << fixed
         << setw(energy_width)
         << network.actual_energy(ee) << " "
         << setw(energy_hist_width) << observations << " "
         << setw(sample_width) << sample_histogram[ee] << " "
         << setw(double_dec + 3) << setprecision(double_dec)
         << log10(exp(1)) * ln_dos[ee] << " "
         << setw(double_dec + 3) << setprecision(double_dec)
         << log10(exp(1)) * ln_weights[ee] << endl;
  }
}

// print expectation value of distances from each pattern at each energy
void network_simulation::print_distances() const {
  if (!fixed_temp) {
    cout << "energy distance" << endl;
    const int energy_width = log10(network.max_energy) + 2;
    for (int ee = energy_range - 1; ee >= 0; ee--) {
      // check that we have sampled distance this energy
      const long observations = all_temp_distance_records[ee];
      if (observations == 0) continue;

      const double val = double(all_temp_distance_logs[ee]) / observations;
      cout << setw(energy_width)
           << network.actual_energy(ee) << " "
           << val * 2 / network.nodes << endl;
    }
  } else {
    const double val =
      double(fixed_temp_distance_log) / fixed_temp_distance_records;
    cout << "distance: " << val * 2 / network.nodes << endl;
  }
}

// print expectation value of each state at each energy
void network_simulation::print_states() const {
  cout << "<s_1>, <s_2>, ..., <s_n>" << endl;
  const int state_dec = 6; // decimal precision of expected state values
  for (int ii = 0; ii < network.nodes; ii++) {
    if (ii > 0) cout << " ";
    const double val = double(state_histograms[ii]) / state_records;
    const int prec = state_dec - int(max(log10(val),0.));
    cout << setw(state_dec + 3) << setprecision(prec) << val * 2 - 1;
  }
  cout << endl;
}

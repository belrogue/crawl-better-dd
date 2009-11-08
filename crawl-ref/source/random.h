#ifndef RANDOM_H
#define RANDOM_H

#include "rng.h"

#include <map>
#include <vector>

bool coinflip();
int div_rand_round( int num, int den );
int div_round_up( int num, int den );
bool one_chance_in(int a_million);
bool x_chance_in_y(int x, int y);
int random2(int max);
int maybe_random2(int x, bool random_factor);
int random_range(int low, int high);
int random_range(int low, int high, int nrolls);
const char* random_choose_string(const char* first, ...);
int random_choose(int first, ...);
int random_choose_weighted(int weight, int first, ...);
int random2avg(int max, int rolls);
int bestroll(int max, int rolls);
int random2limit(int max, int limit);
int binomial_generator(unsigned n_trials, unsigned trial_prob);
int fuzz_value(int val, int lowfuzz, int highfuzz, int naverage = 2);
int roll_dice(int num, int size);

struct dice_def
{
    int num;
    int size;

    dice_def(int n = 0, int s = 0) : num(n), size(s) {}
    int roll() const;
};

dice_def calc_dice(int num_dice, int max_damage);
void scale_dice(dice_def &dice, int threshold = 24);

class rng_save_excursion
{
public:
    rng_save_excursion(long seed) { push_rng_state(); seed_rng(seed); }
    rng_save_excursion()          { push_rng_state(); }
    ~rng_save_excursion()         { pop_rng_state(); }
};

// A defer_rand object represents an infinite tree of random values, allowing
// for a much more functional approach to randomness.  defer_rand values which
// have been used should not be copy-constructed.  Querying the same path
// multiple times will always give the same result.

// An important property of defer_rand is that, except for rounding,
// float(r.random2(X)) / X == float(r.random2(Y)) / Y for all X and Y.  In
// other words:
//
// * The parameter you use on any given call does not matter.
// * The object stores the fraction, not a specific integer.
// * random2() is monotonic in its argument.
class defer_rand
{
    std::vector<unsigned long> bits;
    std::map<int, defer_rand> children;

    bool x_chance_in_y_contd(int x, int y, int index);
public:
    // TODO It would probably be a good idea to have some sort of random
    // number generator API, and the ability to pass RNGs into any function
    // that wants them.
    bool x_chance_in_y(int x, int y) { return x_chance_in_y_contd(x,y,0); }
    bool one_chance_in(int a_million) { return x_chance_in_y(1,a_million); }
    int random2(int maxp1);

    int random_range(int low, int high);
    int random2avg(int max, int rolls);

    defer_rand& operator[] (int i);
};

template<typename Iterator>
int choose_random_weighted(Iterator beg, const Iterator end)
{
    ASSERT(beg < end);

#ifdef DEBUG
    int times_set = 0;
#endif

    int totalweight = 0;
    int count = 0, result = 0;
    while (beg != end)
    {
        totalweight += *beg;
        if (random2(totalweight) < *beg)
        {
            result = count;
#ifdef DEBUG
            times_set++;
#endif
        }
        ++count;
        ++beg;
    }
#ifdef DEBUG
    ASSERT(times_set > 0);
#endif
    return result;
}

#endif


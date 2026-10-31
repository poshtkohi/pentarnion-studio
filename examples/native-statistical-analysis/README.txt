Native statistical workflows (extension19)

distributions-and-histograms.p uses Boost.Math distribution functions and seeded
draws, then histplot. distrand returns values,seed,distribution,method. An omitted
seed is drawn from Studio's shared session RNG and reported in the result.

native-statistic-and-dependent-block-bootstrap.p compiles a fixed-shape statistic
once, resamples ordered blocks in shared-budget workers, and changes a trailing
runtime parameter without recompiling its body. Moving/circular/stationary block
methods expose their dependence assumptions and retain the requested seed.

Generic callback samples/fixed parameters retain the current128-real-coordinate
native callback cap. Built-in statistics support larger bounded real datasets.
Percentile/basic intervals are descriptive finite-resample estimates; selected
block length/stationarity and nominal confidence coverage require scientific
validation for each application. See the detailed language specification.

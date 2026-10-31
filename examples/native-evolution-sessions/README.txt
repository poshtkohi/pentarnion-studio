Native evolutionary session examples (extension18)

checkpoint-islands-and-resume.p explicitly overwrites its demonstration checkpoint
for the first stage, pauses at generation8, then resumes retained CMA adaptation
with a different shared thread limit. Later uninterrupted continuation of an
existing scientific study should use resume=1 without overwriting that study.

constrained-integer-basins-and-verification.p has independently calculable optimum
(2,-1.5), objective0.25 under integer x and x+y<=0.5. Integer coordinates stay fixed
within each continuous local basin. A separate compiled verification objective
adds a known0.01 offset; its score remains separate from the search score.

multiple-native-local-basins.p selects distinct bound-scaled seeds and refines
them using native NLopt and compiler-generated derivatives. A collapsed population
can expose fewer seeds than requested. Finite searches do not prove completeness.

See specs/evolution_sessions_v0_2.txt for byte/work budgets, checkpoint identity,
constrained NSGA-II limitations and the unchanged legacy version1 path.

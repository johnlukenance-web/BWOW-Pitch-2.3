// Offline driver: core_test in.raw out.raw sr channels amount decay sens blockmode
// raw files = planar float32 (all of ch0, then ch1). blockmode: 0 = 512, 1 = random 1..700
#include "../Source/BwowCore.h"
#include <cstdio>
#include <cstdlib>
#include <random>
int main (int argc, char** argv)
{
    if (argc < 9) return 2;
    const double sr = atof (argv[3]); const int ch = atoi (argv[4]);
    const float amt = (float) atof (argv[5]), dec = (float) atof (argv[6]); const int sens = atoi (argv[7]), bm = atoi (argv[8]);
    FILE* f = fopen (argv[1], "rb"); if (! f) return 3;
    fseek (f, 0, SEEK_END); const long bytes = ftell (f); fseek (f, 0, SEEK_SET);
    const long N = bytes / 4 / ch;
    std::vector<float> d ((size_t) (N * ch)); if (fread (d.data(), 4, d.size(), f) != d.size()) return 4; fclose (f);
    bwow::Core core; std::vector<int64_t> log; core.triggerLog = &log;
    core.prepare (sr, ch); core.setParameters (amt, dec, sens);
    std::mt19937 rng (1);
    long pos = 0;
    while (pos < N)
    {
        long bs = bm == 0 ? 512 : 1 + (long) (rng() % 700); bs = std::min (bs, N - pos);
        float* p[2] = { d.data() + pos, ch > 1 ? d.data() + N + pos : nullptr };
        core.process (p, ch, (int) bs); pos += bs;
    }
    f = fopen (argv[2], "wb"); fwrite (d.data(), 4, d.size(), f); fclose (f);
    printf ("latency %d\ntriggers %zu\n", core.getLatencySamples(), log.size());
    for (auto t : log) printf ("T %lld\n", (long long) t);
}

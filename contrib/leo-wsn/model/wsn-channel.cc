#include "wsn-channel.h"

#include "wsn-net-device.h"

#include <ns3/log.h>
#include <ns3/node-list.h>
#include <ns3/node.h>
#include <ns3/simulator.h>

#include <algorithm>
#include <cmath>

namespace ns3
{
namespace leo
{

NS_LOG_COMPONENT_DEFINE("WsnChannel");

// Digitized directly from the paper's Fig. 2 "OK" (successful reception)
// curve by the user via WebPlotDigitizer (automeris.io/wpd/). Second
// revision (Sept 2026): re-extracted at much higher density (562 points,
// vs. 95 in the first pass), including ~6x denser sampling through the
// steep 12-20 dB transition region specifically, and validated as more
// internally consistent than the first pass: summing this curve with the
// paper's companion "No reception" and "CRC error" curves (also
// re-digitized, same status as before -- not currently consumed by
// PrrFromSnrDb) now deviates from the expected 100% by a mean of 0.19
// percentage points across SNR 0-79 dB (vs. 0.42 for the first-pass
// digitization), with a worse-case deviation of 0.79 points (vs. 2.07).
// See data/fig2-digitized/README.md for the raw exports and full
// provenance/caveat notes -- this remains one independent digitization
// of a static published chart, not the authors' own raw measurement
// data. The non-monotonic dips visible in the 24-27 dB range are
// preserved as-digitized rather than smoothed, matching the paper's own
// text: "the PRR was more than 90%, with a few exceptions."
const std::vector<PrrPoint> kPrrCurve = {
    {0.0, 0.0},
    {1.0978, 0.00102},
    {1.2625, 0.00102},
    {1.3918, 0.00102},
    {1.5918, 0.00102},
    {1.8034, 0.00102},
    {2.0622, 0.00084},
    {2.2621, 0.00102},
    {2.4620, 0.0012},
    {2.6619, 0.00102},
    {2.8619, 0.00155},
    {3.0500, 0.00137},
    {3.2852, 0.00137},
    {3.4499, 0.00137},
    {3.6498, 0.00101},
    {3.8497, 0.00137},
    {4.0496, 0.00173},
    {4.2613, 0.00173},
    {4.4965, 0.00173},
    {4.6612, 0.00173},
    {4.8611, 0.00137},
    {5.0493, 0.00173},
    {5.2403, 0.00157},
    {5.4151, 0.00157},
    {5.5898, 0.00157},
    {5.7646, 0.00157},
    {5.9394, 0.00157},
    {6.1141, 0.00156},
    {6.2889, 0.00156},
    {6.4636, 0.00156},
    {6.6384, 0.00156},
    {6.8132, 0.00156},
    {6.9879, 0.00156},
    {7.1627, 0.00156},
    {7.3374, 0.00156},
    {7.5122, 0.00156},
    {7.6870, 0.00156},
    {7.8617, 0.00156},
    {8.0365, 0.00156},
    {8.2112, 0.00156},
    {8.3860, 0.00156},
    {8.5608, 0.00156},
    {8.7355, 0.00156},
    {8.9103, 0.00156},
    {9.0850, 0.00156},
    {9.2598, 0.00156},
    {9.4346, 0.00156},
    {9.6093, 0.00156},
    {9.7841, 0.00156},
    {9.9588, 0.00156},
    {10.1336, 0.00156},
    {10.3084, 0.00156},
    {10.4831, 0.00156},
    {10.6579, 0.00156},
    {10.8326, 0.00155},
    {11.0074, 0.00155},
    {11.1822, 0.00155},
    {11.3569, 0.00155},
    {11.5317, 0.00155},
    {11.7064, 0.00155},
    {11.8812, 0.00155},
    {12.0559, 0.00155},
    {12.2307, 0.00155},
    {12.4055, 0.00222},
    {12.5802, 0.00354},
    {12.7550, 0.00487},
    {12.9297, 0.00687},
    {13.1044, 0.00886},
    {13.2792, 0.01351},
    {13.4539, 0.01683},
    {13.6286, 0.02148},
    {13.8034, 0.02547},
    {13.9781, 0.02879},
    {14.1529, 0.02812},
    {14.3276, 0.02812},
    {14.5024, 0.02613},
    {14.6772, 0.02546},
    {14.8519, 0.02347},
    {15.0267, 0.02347},
    {15.2014, 0.02812},
    {15.3761, 0.03344},
    {15.5509, 0.03875},
    {15.7256, 0.04407},
    {15.9003, 0.04938},
    {16.0313, 0.05602},
    {16.0313, 0.05204},
    {16.0750, 0.062},
    {16.1186, 0.07197},
    {16.1186, 0.06665},
    {16.2059, 0.07994},
    {16.2059, 0.07595},
    {16.2495, 0.08658},
    {16.2932, 0.09323},
    {16.3368, 0.09854},
    {16.3805, 0.10386},
    {16.4241, 0.10984},
    {16.4678, 0.11449},
    {16.5114, 0.12047},
    {16.5550, 0.13043},
    {16.5551, 0.12512},
    {16.6423, 0.1384},
    {16.6424, 0.13442},
    {16.6860, 0.14438},
    {16.7296, 0.15435},
    {16.7296, 0.14903},
    {16.8169, 0.16232},
    {16.8169, 0.15834},
    {16.8605, 0.1683},
    {16.9042, 0.17827},
    {16.9042, 0.17295},
    {16.9915, 0.18358},
    {17.0351, 0.19023},
    {17.0788, 0.19687},
    {17.1661, 0.20484},
    {17.1661, 0.20085},
    {17.2534, 0.21016},
    {17.2971, 0.2168},
    {17.3844, 0.22477},
    {17.4280, 0.23142},
    {17.5153, 0.23939},
    {17.5154, 0.2354},
    {17.6027, 0.2447},
    {17.6463, 0.25135},
    {17.7336, 0.25932},
    {17.7773, 0.26596},
    {17.8646, 0.27393},
    {17.8646, 0.26995},
    {17.9519, 0.27925},
    {17.9956, 0.28589},
    {18.0392, 0.29254},
    {18.0829, 0.29918},
    {18.1265, 0.30582},
    {18.1701, 0.3118},
    {18.2137, 0.32443},
    {18.2138, 0.31645},
    {18.3010, 0.33506},
    {18.3011, 0.32974},
    {18.3883, 0.34834},
    {18.3883, 0.34303},
    {18.4319, 0.35499},
    {18.4756, 0.36163},
    {18.5192, 0.36827},
    {18.5629, 0.37492},
    {18.6065, 0.38156},
    {18.6501, 0.38821},
    {18.6938, 0.39485},
    {18.7374, 0.40149},
    {18.7810, 0.40814},
    {18.8246, 0.4201},
    {18.8247, 0.41478},
    {18.9119, 0.43073},
    {18.9120, 0.42541},
    {18.9556, 0.43737},
    {18.9992, 0.44401},
    {19.0429, 0.45066},
    {19.0865, 0.4573},
    {19.1301, 0.46394},
    {19.1738, 0.47059},
    {19.2174, 0.47723},
    {19.2610, 0.48387},
    {19.3047, 0.49052},
    {19.3483, 0.49716},
    {19.3920, 0.50381},
    {19.4356, 0.51045},
    {19.4792, 0.51709},
    {19.5229, 0.52374},
    {19.5665, 0.53038},
    {19.6102, 0.53702},
    {19.6538, 0.54367},
    {19.6974, 0.55031},
    {19.7411, 0.55696},
    {19.7847, 0.5636},
    {19.8283, 0.57024},
    {19.8720, 0.57689},
    {19.9156, 0.58287},
    {19.9592, 0.59283},
    {19.9593, 0.58752},
    {20.0466, 0.59682},
    {20.0902, 0.60147},
    {20.1776, 0.60678},
    {20.2649, 0.6121},
    {20.3523, 0.61741},
    {20.4396, 0.62273},
    {20.5269, 0.62804},
    {20.6143, 0.63336},
    {20.7016, 0.63867},
    {20.7453, 0.64332},
    {20.8326, 0.64864},
    {20.9199, 0.65395},
    {21.0073, 0.66192},
    {21.0073, 0.65794},
    {21.0509, 0.6679},
    {21.0946, 0.67255},
    {21.1382, 0.6792},
    {21.1818, 0.68584},
    {21.2255, 0.69248},
    {21.2691, 0.69913},
    {21.3128, 0.70577},
    {21.3564, 0.71242},
    {21.4000, 0.7184},
    {21.4437, 0.72305},
    {21.4873, 0.72969},
    {21.5310, 0.73633},
    {21.5746, 0.74298},
    {21.6182, 0.74962},
    {21.6619, 0.7556},
    {21.7055, 0.76557},
    {21.7055, 0.76025},
    {21.7928, 0.77354},
    {21.7929, 0.76955},
    {21.8365, 0.78018},
    {21.8801, 0.78682},
    {21.9237, 0.7928},
    {21.9674, 0.79745},
    {22.0110, 0.8041},
    {22.1421, 0.80808},
    {22.2731, 0.81473},
    {22.4478, 0.8227},
    {22.6225, 0.83067},
    {22.7972, 0.83864},
    {22.9719, 0.84595},
    {23.0592, 0.85127},
    {23.1466, 0.85658},
    {23.2339, 0.8619},
    {23.3212, 0.86721},
    {23.4086, 0.87253},
    {23.4959, 0.87784},
    {23.5833, 0.88316},
    {23.6706, 0.88847},
    {23.7579, 0.89379},
    {23.8453, 0.8991},
    {23.8889, 0.90375},
    {24.0200, 0.90774},
    {24.1947, 0.90774},
    {24.3695, 0.91039},
    {24.5442, 0.91039},
    {24.7190, 0.91039},
    {24.8937, 0.91106},
    {25.0685, 0.90906},
    {25.2433, 0.90242},
    {25.4181, 0.8971},
    {25.5929, 0.89046},
    {25.7677, 0.88382},
    {25.9425, 0.8785},
    {26.1173, 0.88382},
    {26.2920, 0.89179},
    {26.4667, 0.89976},
    {26.6414, 0.90773},
    {26.8161, 0.9157},
    {26.9908, 0.92368},
    {27.1655, 0.92899},
    {27.3402, 0.93563},
    {27.5149, 0.94095},
    {27.6896, 0.94759},
    {27.8643, 0.95291},
    {28.0390, 0.95822},
    {28.2138, 0.96287},
    {28.3885, 0.96752},
    {28.5632, 0.97217},
    {28.7379, 0.97682},
    {28.9127, 0.98081},
    {29.0874, 0.98347},
    {29.2622, 0.98479},
    {29.4369, 0.98479},
    {29.6117, 0.98479},
    {29.7864, 0.98745},
    {29.9612, 0.98745},
    {30.1360, 0.98479},
    {30.3107, 0.9828},
    {30.4855, 0.98081},
    {30.6603, 0.97815},
    {30.8351, 0.97615},
    {31.0098, 0.9735},
    {31.1846, 0.97017},
    {31.3594, 0.96685},
    {31.5342, 0.96286},
    {31.7090, 0.96021},
    {31.8838, 0.95688},
    {32.0585, 0.95489},
    {32.2333, 0.95356},
    {32.4081, 0.9529},
    {32.5828, 0.95223},
    {32.7576, 0.95024},
    {32.9324, 0.95024},
    {33.1071, 0.95157},
    {33.2819, 0.95356},
    {33.4566, 0.95555},
    {33.6313, 0.95821},
    {33.8061, 0.95954},
    {33.9808, 0.96153},
    {34.1556, 0.96419},
    {34.3303, 0.96618},
    {34.5051, 0.96884},
    {34.6798, 0.9715},
    {34.8545, 0.97349},
    {35.0293, 0.97415},
    {35.2041, 0.97415},
    {35.3788, 0.97415},
    {35.5536, 0.97415},
    {35.7283, 0.97415},
    {35.9031, 0.97349},
    {36.0779, 0.97149},
    {36.2527, 0.96883},
    {36.4274, 0.96618},
    {36.6022, 0.96352},
    {36.7770, 0.96086},
    {36.9518, 0.9582},
    {37.1266, 0.95488},
    {37.3014, 0.95156},
    {37.4761, 0.94824},
    {37.6509, 0.94425},
    {37.8257, 0.94093},
    {38.0005, 0.93827},
    {38.1753, 0.93694},
    {38.3500, 0.93628},
    {38.5248, 0.93428},
    {38.6996, 0.93428},
    {38.8743, 0.93229},
    {39.0491, 0.93162},
    {39.2239, 0.93162},
    {39.3986, 0.93162},
    {39.5734, 0.93029},
    {39.7482, 0.92896},
    {39.9229, 0.92963},
    {40.0976, 0.93428},
    {40.2724, 0.93959},
    {40.4471, 0.94491},
    {40.6218, 0.95022},
    {40.7965, 0.95554},
    {40.9712, 0.96019},
    {41.1460, 0.96351},
    {41.3207, 0.96617},
    {41.4955, 0.96816},
    {41.6702, 0.97082},
    {41.8449, 0.97281},
    {42.0197, 0.97281},
    {42.1945, 0.96949},
    {42.3693, 0.9655},
    {42.5441, 0.96218},
    {42.7188, 0.95886},
    {42.8936, 0.95487},
    {43.0684, 0.95354},
    {43.2432, 0.9542},
    {43.4179, 0.95487},
    {43.5927, 0.95553},
    {43.7674, 0.95553},
    {43.9422, 0.95553},
    {44.1170, 0.95354},
    {44.2917, 0.95154},
    {44.4665, 0.95022},
    {44.6413, 0.94756},
    {44.8161, 0.94623},
    {44.9908, 0.9449},
    {45.1656, 0.94822},
    {45.3403, 0.95088},
    {45.5150, 0.95353},
    {45.6898, 0.95686},
    {45.8645, 0.96018},
    {46.0392, 0.96284},
    {46.2140, 0.96549},
    {46.3887, 0.96815},
    {46.5635, 0.97081},
    {46.7382, 0.97346},
    {46.9129, 0.97612},
    {47.0877, 0.97546},
    {47.2625, 0.9728},
    {47.4373, 0.9708},
    {47.6120, 0.96881},
    {47.7868, 0.96615},
    {47.9616, 0.96416},
    {48.1363, 0.96748},
    {48.3111, 0.9708},
    {48.4858, 0.97479},
    {48.6605, 0.97877},
    {48.8352, 0.98276},
    {49.0100, 0.98342},
    {49.1848, 0.9801},
    {49.3596, 0.97612},
    {49.5344, 0.97279},
    {49.7091, 0.96947},
    {49.8839, 0.96548},
    {50.0587, 0.96615},
    {50.2334, 0.96748},
    {50.4082, 0.96881},
    {50.5829, 0.97146},
    {50.7577, 0.97346},
    {50.9324, 0.97478},
    {51.1072, 0.97611},
    {51.2819, 0.97678},
    {51.4567, 0.97678},
    {51.6314, 0.97678},
    {51.8062, 0.97744},
    {51.9809, 0.97877},
    {52.1557, 0.9781},
    {52.3305, 0.9781},
    {52.5052, 0.97677},
    {52.6800, 0.97677},
    {52.8548, 0.97677},
    {53.0295, 0.97877},
    {53.2042, 0.98142},
    {53.3790, 0.98408},
    {53.5537, 0.98674},
    {53.7285, 0.98939},
    {53.9032, 0.99205},
    {54.0780, 0.98873},
    {54.2528, 0.98341},
    {54.4276, 0.97943},
    {54.6024, 0.97411},
    {54.7772, 0.9688},
    {54.9520, 0.96415},
    {55.1267, 0.96547},
    {55.3015, 0.96813},
    {55.4762, 0.96946},
    {55.6509, 0.97145},
    {55.8257, 0.97345},
    {56.0004, 0.9761},
    {56.1752, 0.97942},
    {56.3499, 0.98275},
    {56.5246, 0.98673},
    {56.6994, 0.99005},
    {56.8741, 0.99337},
    {57.0489, 0.99337},
    {57.2236, 0.99072},
    {57.3984, 0.98806},
    {57.5732, 0.9854},
    {57.7480, 0.98274},
    {57.9228, 0.98075},
    {58.0975, 0.98141},
    {58.2723, 0.98208},
    {58.4470, 0.98208},
    {58.6218, 0.98208},
    {58.7965, 0.98208},
    {58.9713, 0.98208},
    {59.1461, 0.98208},
    {59.3208, 0.98207},
    {59.4956, 0.98207},
    {59.6703, 0.98207},
    {59.8451, 0.98075},
    {60.0199, 0.98141},
    {60.1946, 0.98274},
    {60.3694, 0.98473},
    {60.5441, 0.98672},
    {60.7188, 0.98805},
    {60.8936, 0.99004},
    {61.0684, 0.98606},
    {61.2432, 0.98074},
    {61.4180, 0.9741},
    {61.5928, 0.96878},
    {61.7676, 0.96347},
    {61.9424, 0.95882},
    {62.1172, 0.95815},
    {62.2919, 0.95749},
    {62.4667, 0.95682},
    {62.6415, 0.95549},
    {62.8162, 0.95549},
    {62.9910, 0.95616},
    {63.1657, 0.95881},
    {63.3404, 0.96147},
    {63.5152, 0.96346},
    {63.6899, 0.96612},
    {63.8647, 0.96811},
    {64.0394, 0.96678},
    {64.2142, 0.96213},
    {64.3890, 0.95815},
    {64.5638, 0.95416},
    {64.7386, 0.94951},
    {64.9134, 0.94486},
    {65.0008, 0.93888},
    {65.0883, 0.93356},
    {65.1320, 0.92891},
    {65.2194, 0.92493},
    {65.3068, 0.91961},
    {65.3505, 0.91496},
    {65.4380, 0.90965},
    {65.4817, 0.90499},
    {65.5691, 0.90101},
    {65.6565, 0.89569},
    {65.7439, 0.89038},
    {65.7877, 0.88573},
    {65.8751, 0.88041},
    {65.9188, 0.87576},
    {66.0062, 0.87975},
    {66.0498, 0.88573},
    {66.0935, 0.89038},
    {66.1808, 0.89436},
    {66.2245, 0.89968},
    {66.3118, 0.90699},
    {66.3554, 0.91164},
    {66.4428, 0.91961},
    {66.4428, 0.91562},
    {66.5301, 0.9236},
    {66.5738, 0.92891},
    {66.6611, 0.93622},
    {66.7047, 0.94087},
    {66.7920, 0.94884},
    {66.7921, 0.94485},
    {66.8794, 0.95283},
    {66.9230, 0.95748},
    {67.0978, 0.96013},
    {67.2725, 0.9608},
    {67.4473, 0.9608},
    {67.6220, 0.96279},
    {67.7968, 0.96345},
    {67.9716, 0.96213},
    {68.1027, 0.95814},
    {68.2338, 0.95282},
    {68.4086, 0.94751},
    {68.5834, 0.94219},
    {68.7582, 0.93555},
    {68.9330, 0.9309},
    {69.1078, 0.93023},
    {69.2825, 0.93023},
    {69.4573, 0.93156},
    {69.6320, 0.93156},
    {69.8068, 0.93156},
    {69.9815, 0.93156},
    {70.1563, 0.93355},
    {70.3310, 0.93422},
    {70.5058, 0.93621},
    {70.6805, 0.93687},
    {70.8553, 0.93953},
    {71.0300, 0.94219},
    {71.2047, 0.94684},
    {71.3795, 0.95149},
    {71.5542, 0.95614},
    {71.7289, 0.96079},
    {71.9036, 0.96544},
    {72.0784, 0.96278},
    {72.2532, 0.95946},
    {72.4280, 0.95614},
    {72.6028, 0.95215},
    {72.7776, 0.94883},
    {72.9523, 0.9475},
    {73.0833, 0.95414},
    {73.2144, 0.96012},
    {73.3017, 0.9661},
    {73.4764, 0.97341},
    {73.5637, 0.97939},
    {73.6948, 0.98603},
    {73.8258, 0.99268},
    {74.0005, 0.99799},
    {74.1753, 0.99799},
    {74.3500, 0.99799},
    {74.5248, 0.99799},
    {74.6995, 0.99799},
    {74.8743, 0.99799},
    {75.0491, 0.99799},
    {75.2238, 0.99799},
    {75.3986, 0.99799},
    {75.5733, 0.99799},
    {75.7481, 0.99799},
    {75.9229, 0.99799},
    {76.0976, 0.99799},
    {76.2724, 0.99799},
    {76.4471, 0.99798},
    {76.6219, 0.99798},
    {76.7966, 0.99798},
    {76.9714, 0.99798},
    {77.1462, 0.99798},
    {77.3209, 0.99798},
    {77.4957, 0.99798},
    {77.6704, 0.99798},
    {77.8015, 0.99798},
};

double
PrrFromSnrDb(double snrDb)
{
    if (snrDb <= kPrrCurve.front().snrDb)
    {
        return kPrrCurve.front().prr;
    }
    if (snrDb >= kPrrCurve.back().snrDb)
    {
        return kPrrCurve.back().prr;
    }
    for (std::size_t i = 1; i < kPrrCurve.size(); ++i)
    {
        if (snrDb <= kPrrCurve[i].snrDb)
        {
            const PrrPoint& a = kPrrCurve[i - 1];
            const PrrPoint& b = kPrrCurve[i];
            double t = (snrDb - a.snrDb) / (b.snrDb - a.snrDb);
            return a.prr + t * (b.prr - a.prr);
        }
    }
    return kPrrCurve.back().prr; // unreachable
}

double
LinkSignalLossDb(double distanceM, double environmentFactor, double signalLossPerMDbm)
{
    distanceM = std::max(distanceM, 0.01); // avoid log(0)
    // Eq. (18): LSL_AB = -10 * N * log10(l_AB) + P_l
    double lsl = -10.0 * environmentFactor * std::log10(distanceM) + signalLossPerMDbm;
    // Edge-case guard: for distanceM < 1 m, -10*N*log10(d) turns POSITIVE,
    // and could in principle push the total above 0 dB -- i.e. the model
    // would claim propagation *amplifies* the signal, which is not
    // physically meaningful for a passive channel. Clamp at 0 (the
    // authors' own path-loss formula, taken literally, does not include
    // this guard; this is a declared numerical-stability addition, not a
    // reinterpretation of Eq. (18) for any distance >= 1 m where it never
    // triggers).
    return std::min(0.0, lsl);
}

// ---------------------------------------------------------------------
// WsnChannel
// ---------------------------------------------------------------------
TypeId
WsnChannel::GetTypeId()
{
    static TypeId tid = TypeId("ns3::leo::WsnChannel")
                             .SetParent<Channel>()
                             .SetGroupName("LeoWsn")
                             .AddConstructor<WsnChannel>();
    return tid;
}

WsnChannel::WsnChannel()
{
    m_rng = CreateObject<UniformRandomVariable>();
}

int64_t
WsnChannel::AssignStreams(int64_t stream)
{
    m_rng->SetStream(stream);
    return 1;
}

void
WsnChannel::DoDispose()
{
    // Break the strong channel <-> device ownership cycle explicitly.
    for (auto& dev : m_devices)
    {
        if (dev)
        {
            dev->SetChannel(nullptr);
        }
    }
    m_devices.clear();
    m_linkSnrPenaltyDb.clear();
    m_rng = nullptr;
    Channel::DoDispose();
}

void
WsnChannel::Add(Ptr<WsnNetDevice> dev)
{
    m_devices.push_back(dev);
}

std::size_t
WsnChannel::GetNDevices() const
{
    return m_devices.size();
}

Ptr<NetDevice>
WsnChannel::GetDevice(std::size_t /*i*/) const
{
    // WsnNetDevice deliberately does not subclass ns3::NetDevice (see
    // wsn-net-device.h); this override exists only to satisfy the
    // ns3::Channel interface and is not used by this module.
    return nullptr;
}

void
WsnChannel::SetLinkSnrPenaltyDb(uint32_t nodeIdA, uint32_t nodeIdB, double penaltyDb)
{
    m_linkSnrPenaltyDb[{nodeIdA, nodeIdB}] = penaltyDb;
}

void
WsnChannel::ClearLinkSnrPenalties()
{
    m_linkSnrPenaltyDb.clear();
}

void
WsnChannel::Send(Ptr<WsnNetDevice> sender, Ptr<Packet> packet, double txPowerDbm)
{
    NS_LOG_FUNCTION(this << sender << packet << txPowerDbm);
    for (auto& dev : m_devices)
    {
        if (dev == sender)
        {
            continue;
        }
        DeliverIfSuccessful(sender, dev, packet, txPowerDbm);
    }
}

void
WsnChannel::SendUnicast(Ptr<WsnNetDevice> sender, Ptr<Packet> packet, double txPowerDbm, uint32_t intendedNextHopId)
{
    NS_LOG_FUNCTION(this << sender << packet << txPowerDbm << intendedNextHopId);
    for (auto& dev : m_devices)
    {
        if (dev->GetNodeId() == intendedNextHopId)
        {
            DeliverIfSuccessful(sender, dev, packet, txPowerDbm);
            return; // exactly one link evaluated, unlike Send()'s O(N) sweep
        }
    }
    NS_LOG_WARN("SendUnicast: intendedNextHopId " << intendedNextHopId << " not found among attached devices");
}

void
WsnChannel::DeliverIfSuccessful(Ptr<WsnNetDevice> sender, Ptr<WsnNetDevice> receiver, Ptr<Packet> packet, double txPowerDbm)
{
    // Positions are tracked externally through each device's node id and
    // an ns-3 MobilityModel installed on that node by the simulation
    // script; see scratch/leo-topologies.cc.
    Ptr<Node> n1 = NodeList::GetNode(sender->GetNodeId());
    Ptr<Node> n2 = NodeList::GetNode(receiver->GetNodeId());
    Ptr<MobilityModel> m1 = n1->GetObject<MobilityModel>();
    Ptr<MobilityModel> m2 = n2->GetObject<MobilityModel>();
    double distance = m1 && m2 ? m1->GetDistanceFrom(m2) : 1.0;

    double lslDb = LinkSignalLossDb(distance, m_environmentFactor, m_signalLossPerMeterDbm);
    double rssiDbm = txPowerDbm + lslDb; // LSL already includes the sign, Eq. (6)/(18)
    double snrDb = rssiDbm - m_backgroundNoiseDbm;

    // Interference variant (P0-3 fix): apply a per-pair SNR penalty if
    // one was configured for this ordered (sender, receiver) pair,
    // implementing the paper's triangle-hypotenuse degraded-PRR
    // scenario instead of leaving --interference as a documented no-op.
    auto penIt = m_linkSnrPenaltyDb.find({sender->GetNodeId(), receiver->GetNodeId()});
    if (penIt != m_linkSnrPenaltyDb.end())
    {
        snrDb -= penIt->second;
    }

    double prr = PrrFromSnrDb(snrDb);
    bool delivered = m_rng->GetValue(0.0, 1.0) <= prr;

    WsnLinkInfoTag tag;
    tag.rssiDbm = rssiDbm;
    tag.snrDb = snrDb;
    tag.txPowerDbm = txPowerDbm;
    Ptr<Packet> copy = packet->Copy();
    Mac48Address from = sender->GetAddress();

    // Propagation delay is neglected (paper's simulator likewise
    // abstracts the PHY).  Both successful and failed reception attempts
    // execute under the receiver's node context.  A failed decode is
    // reported only to the energy-accounting callback; no protocol packet
    // is delivered upward.
    if (delivered)
    {
        Simulator::ScheduleWithContext(receiver->GetNodeId(),
                                       Seconds(0),
                                       &WsnNetDevice::Receive,
                                       receiver,
                                       copy,
                                       from,
                                       tag);
    }
    else
    {
        Simulator::ScheduleWithContext(receiver->GetNodeId(),
                                       Seconds(0),
                                       &WsnNetDevice::ReceiveFailed,
                                       receiver,
                                       copy,
                                       from,
                                       tag);
    }
}

} // namespace leo
} // namespace ns3

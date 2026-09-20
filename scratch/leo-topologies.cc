/*
 * leo-topologies.cc
 *
 * Reproduces the experimental methodology of Section 4 of:
 *   Fitos et al., "LEO: An innovative metric for energy-efficient routing
 *   in wireless sensor networks," Internet of Things 29 (2025) 101472.
 *
 * For each of the six node layouts of Fig. 4 (around, one-side, U-shaped,
 * line, circle, triangle - the last with and without simulated
 * interference) and for each of several environmental-factor values, the
 * script runs the connection phase (NET_SCAN + CONNECT + one path
 * discovery per node) followed by the ping phase (gateway pings every
 * node 10 times at 1 s intervals, 100-byte payload per Section 4), once
 * per metric (Hop-count, ZigBee LQI, LEO), and writes per-run network
 * energy (mWs, broken down by frame type) and ping outcome counts to a
 * CSV file for statistical analysis in the target publication.
 *
 * NOTE on run counts: the paper states both "24 simulation files" run "20
 * times" per file per metric (24*20*3 = 1440) AND, in the same paragraph,
 * "the simulation was run 1920 times" -- these two statements are
 * arithmetically inconsistent in the source paper itself (1440 != 1920).
 * This driver does not attempt to force either number; choose your own
 * (layout x envFactor) grid and `--runs` value and report it explicitly
 * rather than claiming to match either paper figure.
 *
 * Usage:
 *   ./ns3 run "leo-topologies --layout=around --envFactor=2.5 --metric=leo
 *              --runs=20 --seedBase=1"
 */
#include "ns3/core-module.h"

#include "ns3/leo-wsn-module.h" // aggregated header, see note in README if your
                                 // ns-3 version does not auto-generate this;
                                 // otherwise include the individual headers below:
// #include "ns3/atpc.h"
// #include "ns3/neighbor-table.h"
// #include "ns3/route-metrics.h"
// #include "ns3/wsn-channel.h"
// #include "ns3/wsn-net-device.h"
// #include "ns3/wsn-routing-app.h"
// #include "ns3/nrf52840-current-table.h"

#include "ns3/constant-position-mobility-model.h"
#include "ns3/mac48-address.h"
#include "ns3/mobility-model.h"
#include "ns3/node-container.h"
#include "ns3/node-list.h"
#include "ns3/position-allocator.h"
#include "ns3/random-walk-2d-mobility-model.h"
#include "ns3/random-waypoint-mobility-model.h"
#include "ns3/rectangle.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace ns3;
using namespace ns3::leo;

NS_LOG_COMPONENT_DEFINE("LeoTopologies");

// Stable RNG stream namespaces. These identities are deliberately
// independent of object-allocation order so paired runs across metrics
// keep the same stochastic component identities.
static constexpr int64_t kChannelRngStream = 10;
static constexpr int64_t kMobilityRngStreamBase = 1000;
static constexpr int64_t kMobilityRngStreamStride = 16;

static int64_t
MobilityRngStreamForNode(uint32_t nodeId)
{
    return kMobilityRngStreamBase +
           static_cast<int64_t>(nodeId) * kMobilityRngStreamStride;
}

// ---------------------------------------------------------------------
// Layouts of Fig. 4. Positions are illustrative and parameterized by a
// nominal inter-node spacing "spacingM"; the *absolute* coordinates used
// by the authors' MeshProtocolSimulator are not published, so these are a
// faithful reproduction of each layout's topology/shape only. This must
// be reported as a modeling choice in any resulting publication, and the
// spacing should be swept as a sensitivity parameter.
//
// \param hypotenuseNodeIds if non-null and layout=="triangle", filled with
//        the node indices placed on the hypotenuse, so the caller can
//        apply the interference variant (Section 4) to exactly those
//        nodes' links.
// ---------------------------------------------------------------------
std::vector<Vector>
BuildLayout(const std::string& layout,
            uint32_t nNodes,
            double spacingM,
            std::vector<uint32_t>* hypotenuseNodeIds = nullptr)
{
    std::vector<Vector> pos;
    if (layout == "around")
    {
        // gateway (node 0) at the center, others on a ring around it
        pos.push_back(Vector(0, 0, 0));
        uint32_t ring = nNodes - 1;
        for (uint32_t i = 0; i < ring; ++i)
        {
            double angle = 2.0 * M_PI * i / ring;
            pos.push_back(Vector(spacingM * std::cos(angle), spacingM * std::sin(angle), 0));
        }
    }
    else if (layout == "one-side")
    {
        // gateway at one edge, others clustered to one side
        pos.push_back(Vector(0, 0, 0));
        uint32_t ring = nNodes - 1;
        for (uint32_t i = 0; i < ring; ++i)
        {
            double angle = M_PI * i / std::max<uint32_t>(1, ring - 1) - M_PI / 2.0;
            pos.push_back(Vector(spacingM + spacingM * std::cos(angle), spacingM * std::sin(angle), 0));
        }
    }
    else if (layout == "u-shaped")
    {
        // Proper "U": gateway at the top of the LEFT arm, down the left
        // arm, across the bottom, then UP the right arm (Fig. 4c). A
        // previous version only had one vertical arm + one horizontal
        // segment (an "L", not a "U") -- fixed per external review (P1-2).
        pos.push_back(Vector(0, 0, 0)); // gateway, top of left arm
        uint32_t remaining = nNodes - 1;
        uint32_t leftLen = remaining / 3;
        uint32_t bottomLen = remaining / 3;
        uint32_t rightLen = remaining - leftLen - bottomLen;
        for (uint32_t i = 1; i <= leftLen; ++i)
        {
            pos.push_back(Vector(0, -spacingM * i, 0));
        }
        double bottomY = -spacingM * leftLen;
        for (uint32_t i = 1; i <= bottomLen; ++i)
        {
            pos.push_back(Vector(spacingM * i, bottomY, 0));
        }
        double rightX = spacingM * bottomLen;
        for (uint32_t i = 1; i <= rightLen; ++i)
        {
            pos.push_back(Vector(rightX, bottomY + spacingM * i, 0));
        }
    }
    else if (layout == "line")
    {
        for (uint32_t i = 0; i < nNodes; ++i)
        {
            pos.push_back(Vector(spacingM * i, 0, 0));
        }
        // gateway is the first node in this layout, per Fig. 4(d)
    }
    else if (layout == "circle")
    {
        double radius = spacingM * nNodes / (2.0 * M_PI);
        for (uint32_t i = 0; i < nNodes; ++i)
        {
            double angle = 2.0 * M_PI * i / nNodes;
            pos.push_back(Vector(radius * std::cos(angle), radius * std::sin(angle), 0));
        }
    }
    else if (layout == "triangle")
    {
        // Proper right-triangle with all THREE sides (vertical leg, base,
        // hypotenuse). A previous version omitted the base entirely,
        // producing an open wedge rather than a closed triangle -- fixed
        // per external review (P1-2).
        pos.push_back(Vector(0, 0, 0)); // gateway at the right angle
        uint32_t remaining = nNodes - 1;
        uint32_t legLen = remaining / 3;
        uint32_t baseLen = remaining / 3;
        uint32_t hypLen = remaining - legLen - baseLen;
        for (uint32_t i = 1; i <= legLen; ++i)
        {
            pos.push_back(Vector(0, spacingM * i, 0));
        }
        for (uint32_t i = 1; i <= baseLen; ++i)
        {
            pos.push_back(Vector(spacingM * i, 0, 0));
        }
        double legTopY = spacingM * legLen;
        double baseEndX = spacingM * baseLen;
        for (uint32_t i = 1; i <= hypLen; ++i)
        {
            double t = static_cast<double>(i) / (hypLen + 1);
            double x = baseEndX * t;
            double y = legTopY * (1.0 - t);
            pos.push_back(Vector(x, y, 0));
            if (hypotenuseNodeIds)
            {
                hypotenuseNodeIds->push_back(static_cast<uint32_t>(pos.size() - 1));
            }
        }
    }
    else
    {
        NS_FATAL_ERROR("Unknown layout: " << layout);
    }
    return pos;
}

MetricType
ParseMetric(const std::string& s)
{
    if (s == "hopcount")
    {
        return MetricType::HOP_COUNT;
    }
    if (s == "lqi")
    {
        return MetricType::ZIGBEE_LQI;
    }
    if (s == "lqi-literal")
    {
        // Eq. (2)-(3) applied exactly as published, no correction -- see
        // ZigbeeLqiLiteralMetric's doc comment in route-metrics.h for the
        // verified directional-inconsistency finding this variant exposes.
        return MetricType::ZIGBEE_LQI_LITERAL;
    }
    if (s == "leo")
    {
        return MetricType::LEO;
    }
    NS_FATAL_ERROR("Unknown metric: " << s << " (expected hopcount|lqi|lqi-literal|leo)");
}

const char*
FrameTypeName(FrameType t)
{
    switch (t)
    {
    case FrameType::NET_SCAN:
        return "netScan";
    case FrameType::CONNECT:
        return "connect";
    case FrameType::PATH_DISCOVERY:
        return "pathDiscovery";
    case FrameType::PATH_DISCOVERY_REPLY:
        return "pathDiscoveryReply";
    case FrameType::PING:
        return "ping";
    case FrameType::PING_REPLY:
        return "pingReply";
    case FrameType::ACK:
        return "ack";
    }
    return "unknown";
}

int
main(int argc, char* argv[])
{
    std::string layout = "circle";
    uint32_t nNodes = 20;
    double spacingM = 15.0;
    double envFactor = 2.0;
    double signalLossPerMDbm = -64.0;   // measured by the authors, Section 4
    double backgroundNoiseDbm = -116.0; // see README's SNR-budget note
    std::string metricStr = "leo";
    uint32_t runs = 20; // matches "the simulation was run 20 times"
    uint32_t seedBase = 1;
    bool interference = false; // triangle-with-interference variant, Section 4
    // The paper does not publish a numeric SNR/PER penalty for the
    // interference variant, only that hypotenuse-node PER is degraded
    // relative to the 5% baseline; this dB value is therefore a declared,
    // tunable stand-in (LeapSpace review's "conservative, disclosed
    // substitute" guidance) rather than a published constant. Sweep it if
    // you need a sensitivity argument.
    double interferenceDb = 15.0;
    uint32_t pingPayloadBytes = 100; // Section 4: "ping packet data part contains 100 bytes"
    double bitrateBps = 1.0e6;       // Section 3: nRF52840 supports 1 or 2 Mbit/s; default 1
    // Fixed MAC/PHY turnaround per frame (preamble, sync, CRC, radio
    // ramp-up/down) NOT specified numerically by the paper; declared
    // assumption, default 100 us.
    double radioOverheadS = 100.0e-6;
    uint32_t macMaxRetries = 4;     // finite-ARQ operational substitute; source timing/cap not fully published
    double ackTimeoutS = 0.05;      // reconstruction assumption
    double discoveryTimeoutS = 1.0; // R1 liveness guard; reconstruction assumption
    bool boundEq17ToRMax = true;    // v1.0.0 behavior; false = literal Eq.17 sensitivity
    uint32_t relayCap = 3;          // source-unspecified discovery re-relay cap; R4 design factor
    bool useNrf52840Energy = false; // accounting mode only; physical TX remains discrete in both modes

    // R4 provenance identity. R5's manifest runner must populate these for
    // production data; defaults remain useful for bounded manual smoke runs.
    std::string experimentId = "UNSPECIFIED";
    std::string scenarioSet = "UNSPECIFIED";
    std::string scenarioId = "UNSPECIFIED";
    std::string manifestSha256 = "UNSPECIFIED";
    std::string routeCsv = "";
    // HFXO crystal choice for RadioParameters::hfxoStandbyCurrentMa (only
    // matters when useNrf52840Energy=true). "none" (default) reproduces
    // RADIO-only current (Section 6.20.15's own scope, excludes the
    // clock); the paper does not specify which crystal was used, so any
    // other choice here is a declared assumption you must report.
    std::string hfxoCrystal = "none";
    std::string outCsv = "leo-results.csv";

    // ---------------------------------------------------------------
    // Mobility (see README's mobility-methodology section for the full
    // rationale). Two independent models, mutually exclusive:
    //   "walk"     -- RandomWalk2dMobilityModel, bounded to a small box
    //                 CENTERED ON EACH NODE'S ORIGINAL Fig.4 LAYOUT
    //                 POSITION, preserving the macro-topology while
    //                 adding realistic local drift/jitter.
    //   "waypoint" -- RandomWaypointMobilityModel over the FULL layout
    //                 bounding box (+ margin), an unconstrained
    //                 MANET-style stress test. minSpeed is enforced
    //                 strictly > 0 to avoid the well-documented ns-3/RWP
    //                 average-speed decay artifact (Yoon, Liu & Noble,
    //                 "Random waypoint considered harmful", INFOCOM 2003).
    //                 Under this mode the six Fig.4 layouts define only
    //                 the INITIAL topology, not an invariant.
    // "none" (default) reproduces the original static-node behavior.
    // ---------------------------------------------------------------
    std::string mobility = "none"; // none|walk|waypoint
    double mobilityBoxFrac = 0.3;  // walk: half-width of the per-node box, as a fraction of spacingM
    double mobilitySpeedMin = 0.5; // m/s, both modes; kept > 0 for waypoint (see above)
    double mobilitySpeedMax = 2.0; // m/s, both modes
    double mobilityPauseMin = 0.0; // s, waypoint only
    double mobilityPauseMax = 2.0; // s, waypoint only
    // Simulated time discarded BEFORE the connection phase begins, to let
    // the mobility process reach a statistically steady state (mandatory
    // for waypoint mode per the artifact above; optional but harmless for
    // walk mode, where 0 is a reasonable default since a bounded random
    // walk has no equivalent long-transient issue).
    double mobilityWarmupS = 0.0;
    bool mobilityDiagnostics = false;     // log average node degree over time, see LogMobilityDiagnostics
    double mobilityDiagnosticsPeriodS = 2.0;
    std::string mobilityDiagCsv = "mobility-diagnostics.csv";

    // Sensitivity-analysis hooks (test 5 of the pre-publication checklist):
    // two documented-but-unpublished constants, now sweepable without
    // recompiling.
    double emaAlpha = 0.2;          // matches kDefaultEmaAlpha in neighbor-table.h
    double discoveryWindowS = 0.15; // matches the default in wsn-routing-app.h

    CommandLine cmd(__FILE__);
    cmd.AddValue("layout", "around|one-side|u-shaped|line|circle|triangle", layout);
    cmd.AddValue("nNodes", "number of nodes including the gateway", nNodes);
    cmd.AddValue("spacingM", "nominal inter-node spacing [m]", spacingM);
    cmd.AddValue("envFactor", "environmental factor N in Eq. (18)", envFactor);
    cmd.AddValue("signalLossPerMDbm", "signal-loss constant in Eq. (18), dBm", signalLossPerMDbm);
    cmd.AddValue("backgroundNoiseDbm", "noise floor N in Eq. (5)/(9), dBm", backgroundNoiseDbm);
    cmd.AddValue("metric", "hopcount|lqi|lqi-literal|leo", metricStr);
    cmd.AddValue("runs", "number of independent repetitions", runs);
    cmd.AddValue("seedBase", "base RNG seed", seedBase);
    cmd.AddValue("interference", "apply the triangle-hypotenuse interference variant", interference);
    cmd.AddValue("interferenceDb", "SNR penalty applied to hypotenuse-node links, dB", interferenceDb);
    cmd.AddValue("pingPayloadBytes", "ping payload size in bytes (paper: 100)", pingPayloadBytes);
    cmd.AddValue("bitrateBps", "PHY bitrate used for airtime/energy accounting", bitrateBps);
    cmd.AddValue("radioOverheadS", "fixed per-frame MAC/PHY turnaround, s", radioOverheadS);
    cmd.AddValue("macMaxRetries",
                 "stop-and-wait ARQ retry cap (operational reconstruction; sensitivity parameter)",
                 macMaxRetries);
    cmd.AddValue("ackTimeoutS",
                 "ACK wait timeout in seconds (unpublished reconstruction parameter)",
                 ackTimeoutS);
    cmd.AddValue("discoveryTimeoutS",
                 "path-discovery liveness timeout in seconds (R1 reconstruction parameter)",
                 discoveryTimeoutS);
    cmd.AddValue("boundEq17ToRMax",
                 "true = v1.0.0 bounded Eq.17 reconstruction; false = literal unbounded Eq.17",
                 boundEq17ToRMax);
    cmd.AddValue("relayCap",
                 "per-node PATH_DISCOVERY re-relay cap; source-unspecified R4 design factor",
                 relayCap);
    cmd.AddValue("experimentId", "R4/R5 experiment identity written to every output row", experimentId);
    cmd.AddValue("scenarioSet", "manifest scenario-set identity written to every output row", scenarioSet);
    cmd.AddValue("scenarioId", "manifest scenario identity written to every output row", scenarioId);
    cmd.AddValue("manifestSha256", "SHA256 of the frozen experiment manifest", manifestSha256);
    cmd.AddValue("routeCsv",
                 "optional per-PING route/status evidence CSV; required by R5 production manifest",
                 routeCsv);
    cmd.AddValue("useNrf52840Energy",
                 "energy accounting only: false=Eq.14-style proxy, true=nRF52840 RADIO-current model; "
                 "physical TX levels are discrete in both modes",
                 useNrf52840Energy);
    cmd.AddValue("hfxoCrystal",
                 "none|epson_fa128|epson_fa20h|epson_tsx3225|ndk_nx1612aa|ndk_nx1210ab -- adds "
                 "that crystal's HFXO standby current to the nRF52840 energy model (only used "
                 "when useNrf52840Energy=true); 'none' (default) excludes clock current, "
                 "matching Section 6.20.15's own scope",
                 hfxoCrystal);
    cmd.AddValue("outCsv", "output CSV path", outCsv);
    cmd.AddValue("mobility", "none|walk|waypoint -- see file header for the methodology behind each", mobility);
    cmd.AddValue("mobilityBoxFrac", "walk mode: per-node box half-width as a fraction of spacingM", mobilityBoxFrac);
    cmd.AddValue("mobilitySpeedMin", "mobility speed lower bound, m/s (kept > 0 for waypoint)", mobilitySpeedMin);
    cmd.AddValue("mobilitySpeedMax", "mobility speed upper bound, m/s", mobilitySpeedMax);
    cmd.AddValue("mobilityPauseMin", "waypoint mode: pause time lower bound, s", mobilityPauseMin);
    cmd.AddValue("mobilityPauseMax", "waypoint mode: pause time upper bound, s", mobilityPauseMax);
    cmd.AddValue("mobilityWarmupS",
                 "simulated time discarded before the connection phase begins, letting mobility "
                 "reach steady state (recommended >0 for waypoint mode)",
                 mobilityWarmupS);
    cmd.AddValue("mobilityDiagnostics",
                 "log average node degree (PRR>0.5 threshold) over time to mobilityDiagCsv, for "
                 "pre-flight validation of the mobility model before a full sweep",
                 mobilityDiagnostics);
    cmd.AddValue("mobilityDiagnosticsPeriodS", "sampling period for mobility diagnostics, s", mobilityDiagnosticsPeriodS);
    cmd.AddValue("mobilityDiagCsv", "output path for mobility diagnostics", mobilityDiagCsv);
    cmd.AddValue("emaAlpha", "EMA smoothing factor for R_RX/R_TX/Eq.(1) p_l, [0,1] (sensitivity sweep)", emaAlpha);
    cmd.AddValue("discoveryWindowS",
                 "destination-side best-route candidate-collection window, s (sensitivity sweep)",
                 discoveryWindowS);
    cmd.Parse(argc, argv);

    // R3 fail-fast configuration validation. Reject invalid/unsafe inputs
    // before opening output files, creating nodes, or scheduling events.
    NS_ABORT_MSG_IF(nNodes < 2, "--nNodes must be >= 2, got " << nNodes);
    NS_ABORT_MSG_IF(runs == 0, "--runs must be >= 1");
    NS_ABORT_MSG_IF(!std::isfinite(spacingM) || spacingM <= 0.0,
                    "--spacingM must be finite and > 0, got " << spacingM);
    NS_ABORT_MSG_IF(!std::isfinite(envFactor) || envFactor <= 0.0,
                    "--envFactor must be finite and > 0, got " << envFactor);
    NS_ABORT_MSG_IF(!std::isfinite(signalLossPerMDbm) || signalLossPerMDbm > 0.0,
                    "--signalLossPerMDbm must be finite and <= 0, got " << signalLossPerMDbm);
    NS_ABORT_MSG_IF(!std::isfinite(backgroundNoiseDbm),
                    "--backgroundNoiseDbm must be finite");
    NS_ABORT_MSG_IF(!std::isfinite(interferenceDb) || interferenceDb < 0.0,
                    "--interferenceDb must be finite and >= 0, got " << interferenceDb);
    NS_ABORT_MSG_IF(!std::isfinite(bitrateBps) || bitrateBps <= 0.0,
                    "--bitrateBps must be finite and > 0, got " << bitrateBps);
    NS_ABORT_MSG_IF(!std::isfinite(radioOverheadS) || radioOverheadS < 0.0,
                    "--radioOverheadS must be finite and >= 0, got " << radioOverheadS);
    NS_ABORT_MSG_IF(macMaxRetries > kMaxRetransmissionField,
                    "--macMaxRetries must be <= "
                        << static_cast<uint32_t>(kMaxRetransmissionField)
                        << " so it fits the protocol retransmission field");
    NS_ABORT_MSG_IF(relayCap == 0 || relayCap > 255,
                    "--relayCap must be in [1,255], got " << relayCap);
    NS_ABORT_MSG_IF(!std::isfinite(ackTimeoutS) || ackTimeoutS <= 0.0,
                    "--ackTimeoutS must be finite and > 0, got " << ackTimeoutS);
    NS_ABORT_MSG_IF(!std::isfinite(discoveryTimeoutS) || discoveryTimeoutS <= 0.0,
                    "--discoveryTimeoutS must be finite and > 0, got " << discoveryTimeoutS);
    NS_ABORT_MSG_IF(!std::isfinite(mobilityBoxFrac) || mobilityBoxFrac < 0.0,
                    "--mobilityBoxFrac must be finite and >= 0, got " << mobilityBoxFrac);
    NS_ABORT_MSG_IF(!std::isfinite(mobilitySpeedMin) ||
                        !std::isfinite(mobilitySpeedMax) ||
                        mobilitySpeedMin < 0.0 ||
                        mobilitySpeedMax < mobilitySpeedMin,
                    "mobility speed range must be finite with 0 <= min <= max");
    NS_ABORT_MSG_IF(!std::isfinite(mobilityPauseMin) ||
                        !std::isfinite(mobilityPauseMax) ||
                        mobilityPauseMin < 0.0 ||
                        mobilityPauseMax < mobilityPauseMin,
                    "mobility pause range must be finite with 0 <= min <= max");
    NS_ABORT_MSG_IF(!std::isfinite(mobilityWarmupS) || mobilityWarmupS < 0.0,
                    "--mobilityWarmupS must be finite and >= 0, got " << mobilityWarmupS);
    NS_ABORT_MSG_IF(mobilityDiagnostics &&
                        (!std::isfinite(mobilityDiagnosticsPeriodS) ||
                         mobilityDiagnosticsPeriodS <= 0.0),
                    "--mobilityDiagnosticsPeriodS must be finite and > 0 when diagnostics are enabled");
    NS_ABORT_MSG_IF(!std::isfinite(emaAlpha) || emaAlpha < 0.0 || emaAlpha > 1.0,
                    "--emaAlpha must be finite and in [0,1], got " << emaAlpha);
    NS_ABORT_MSG_IF(!std::isfinite(discoveryWindowS) || discoveryWindowS < 0.0,
                    "--discoveryWindowS must be finite and >= 0, got " << discoveryWindowS);
    NS_ABORT_MSG_IF(outCsv.empty(), "--outCsv must not be empty");
    auto validateCsvToken = [](const std::string& name, const std::string& value) {
        NS_ABORT_MSG_IF(value.find(',') != std::string::npos ||
                            value.find('\n') != std::string::npos ||
                            value.find('\r') != std::string::npos,
                        "--" << name << " must not contain comma/newline characters");
    };
    validateCsvToken("experimentId", experimentId);
    validateCsvToken("scenarioSet", scenarioSet);
    validateCsvToken("scenarioId", scenarioId);
    validateCsvToken("manifestSha256", manifestSha256);
    validateCsvToken("hfxoCrystal", hfxoCrystal);
    validateCsvToken("mobility", mobility);
    validateCsvToken("metric", metricStr);
    validateCsvToken("layout", layout);

    NS_ABORT_MSG_IF(!routeCsv.empty() && routeCsv == outCsv,
                    "--routeCsv and --outCsv must be different files");
    NS_ABORT_MSG_IF(mobilityDiagnostics && !routeCsv.empty() && routeCsv == mobilityDiagCsv,
                    "--routeCsv and --mobilityDiagCsv must be different files");
    NS_ABORT_MSG_IF(mobilityDiagnostics && mobilityDiagCsv.empty(),
                    "--mobilityDiagCsv must not be empty when diagnostics are enabled");
    NS_ABORT_MSG_IF(mobilityDiagnostics && outCsv == mobilityDiagCsv,
                    "--outCsv and --mobilityDiagCsv must be different files");

    MetricType metricType = ParseMetric(metricStr);

    const std::vector<FrameType> kAllFrameTypes = {FrameType::NET_SCAN,
                                                     FrameType::CONNECT,
                                                     FrameType::PATH_DISCOVERY,
                                                     FrameType::PATH_DISCOVERY_REPLY,
                                                     FrameType::PING,
                                                     FrameType::PING_REPLY,
                                                     FrameType::ACK};

    bool writeHeader = true;
    {
        std::ifstream existing(outCsv);
        writeHeader = !existing.good() || existing.peek() == std::ifstream::traits_type::eof();
    }
    std::ofstream csv(outCsv, std::ios::app);
    NS_ABORT_MSG_IF(!csv.is_open() || !csv.good(),
                    "failed to open output CSV for append: " << outCsv);
    if (writeHeader)
    {
        csv << "experimentId,scenarioSet,scenarioId,manifestSha256,"
               "layout,envFactor,metric,interference,relayCap,run,seedBase,ns3Run,"
               "nNodes,spacingM,signalLossPerMDbm,backgroundNoiseDbm,interferenceDb,"
               "mobility,mobilityBoxFrac,mobilitySpeedMin,mobilitySpeedMax,"
               "mobilityPauseMin,mobilityPauseMax,mobilityWarmupS,"
               "emaAlpha,discoveryWindowS,discoveryTimeoutS,macMaxRetries,ackTimeoutS,"
               "bitrateBps,radioOverheadS,useNrf52840Energy,hfxoCrystal,"
               "boundEq17ToRMax,pingPayloadBytes,atpcMode,"
               "totalEnergyMWs,pingTimeouts,pingNoRoute,pingSent";
        for (FrameType t : kAllFrameTypes)
        {
            csv << ",energy_" << FrameTypeName(t);
        }
        csv << "\n";
    }

    std::ofstream routeOut;
    if (!routeCsv.empty())
    {
        bool writeRouteHeader = true;
        {
            std::ifstream existingRoute(routeCsv);
            writeRouteHeader =
                !existingRoute.good() || existingRoute.peek() == std::ifstream::traits_type::eof();
        }
        routeOut.open(routeCsv, std::ios::app);
        NS_ABORT_MSG_IF(!routeOut.is_open() || !routeOut.good(),
                        "failed to open route evidence CSV for append: " << routeCsv);
        if (writeRouteHeader)
        {
            routeOut << "experimentId,scenarioSet,scenarioId,manifestSha256,"
                        "layout,envFactor,metric,interference,relayCap,run,seedBase,ns3Run,"
                        "targetId,seq,status,routeFingerprint,routeHopCount\n";
        }
    }

    std::ofstream diagCsv;
    if (mobilityDiagnostics)
    {
        bool writeDiagHeader = true;
        {
            std::ifstream existingDiag(mobilityDiagCsv);
            writeDiagHeader = !existingDiag.good() || existingDiag.peek() == std::ifstream::traits_type::eof();
        }
        diagCsv.open(mobilityDiagCsv, std::ios::app);
        NS_ABORT_MSG_IF(!diagCsv.is_open() || !diagCsv.good(),
                        "failed to open mobility diagnostics CSV for append: " << mobilityDiagCsv);
        if (writeDiagHeader)
        {
            diagCsv << "layout,envFactor,metric,mobility,run,simTimeS,avgNodeDegree\n";
        }
    }

    for (uint32_t run = 0; run < runs; ++run)
    {
        RngSeedManager::SetSeed(seedBase);
        RngSeedManager::SetRun(run + 1);
        RngSeedManager::ResetNextStreamIndex();

        NodeContainer nodes;
        nodes.Create(nNodes);

        std::vector<uint32_t> hypotenuseIds;
        std::vector<Vector> positions = BuildLayout(layout, nNodes, spacingM, &hypotenuseIds);
        NS_ABORT_MSG_IF(positions.size() != nNodes, "layout builder returned wrong node count");

        for (uint32_t i = 0; i < nNodes; ++i)
        {
            if (mobility == "none")
            {
                Ptr<ConstantPositionMobilityModel> mm = CreateObject<ConstantPositionMobilityModel>();
                mm->SetPosition(positions[i]);
                nodes.Get(i)->AggregateObject(mm);
            }
            else if (mobility == "walk")
            {
                // Bounded local drift: box centered on this node's own
                // Fig.4 position, preserving the macro-topology.
                double half = mobilityBoxFrac * spacingM;
                Rectangle box(positions[i].x - half,
                               positions[i].x + half,
                               positions[i].y - half,
                               positions[i].y + half);
                Ptr<RandomWalk2dMobilityModel> mm = CreateObject<RandomWalk2dMobilityModel>();
                mm->SetAttribute("Bounds", RectangleValue(box));
                mm->SetAttribute("Mode", StringValue("Time"));
                mm->SetAttribute("Time", TimeValue(Seconds(2.0))); // direction/speed re-picked every 2 s
                std::ostringstream speedStream;
                speedStream << "ns3::UniformRandomVariable[Min=" << mobilitySpeedMin << "|Max=" << mobilitySpeedMax
                            << "]";
                mm->SetAttribute("Speed", StringValue(speedStream.str()));
                const int64_t consumed = mm->AssignStreams(MobilityRngStreamForNode(i));
                NS_ABORT_MSG_IF(consumed > kMobilityRngStreamStride,
                                "RandomWalk2dMobilityModel exceeded reserved RNG stream block");
                nodes.Get(i)->AggregateObject(mm);
                mm->SetPosition(positions[i]);
            }
            else if (mobility == "waypoint")
            {
                NS_ABORT_MSG_IF(mobilitySpeedMin <= 0.0,
                                 "waypoint mode requires --mobilitySpeedMin > 0 (avoids the "
                                 "well-documented ns-3/RandomWaypoint average-speed decay "
                                 "artifact -- see file header)");
                Ptr<RandomWaypointMobilityModel> mm = CreateObject<RandomWaypointMobilityModel>();
                std::ostringstream speedStream;
                speedStream << "ns3::UniformRandomVariable[Min=" << mobilitySpeedMin << "|Max=" << mobilitySpeedMax
                            << "]";
                mm->SetAttribute("Speed", StringValue(speedStream.str()));
                std::ostringstream pauseStream;
                pauseStream << "ns3::UniformRandomVariable[Min=" << mobilityPauseMin << "|Max=" << mobilityPauseMax
                            << "]";
                mm->SetAttribute("Pause", StringValue(pauseStream.str()));
                // Waypoints drawn from the layout's own bounding box plus
                // one spacingM margin on every side; under this mode the
                // Fig.4 layouts define only the INITIAL topology (see file
                // header).
                double minX = positions[0].x, maxX = positions[0].x;
                double minY = positions[0].y, maxY = positions[0].y;
                for (const auto& p : positions)
                {
                    minX = std::min(minX, p.x);
                    maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y);
                    maxY = std::max(maxY, p.y);
                }
                Ptr<RandomRectanglePositionAllocator> alloc = CreateObject<RandomRectanglePositionAllocator>();
                std::ostringstream xStream, yStream;
                xStream << "ns3::UniformRandomVariable[Min=" << (minX - spacingM) << "|Max=" << (maxX + spacingM)
                        << "]";
                yStream << "ns3::UniformRandomVariable[Min=" << (minY - spacingM) << "|Max=" << (maxY + spacingM)
                        << "]";
                alloc->SetAttribute("X", StringValue(xStream.str()));
                alloc->SetAttribute("Y", StringValue(yStream.str()));
                mm->SetAttribute("PositionAllocator", PointerValue(alloc));
                const int64_t consumed = mm->AssignStreams(MobilityRngStreamForNode(i));
                NS_ABORT_MSG_IF(consumed > kMobilityRngStreamStride,
                                "RandomWaypointMobilityModel exceeded reserved RNG stream block");
                nodes.Get(i)->AggregateObject(mm);
                mm->SetPosition(positions[i]); // initial position: the node's Fig.4 layout coordinate
            }
            else
            {
                NS_FATAL_ERROR("Unknown --mobility: " << mobility << " (expected none|walk|waypoint)");
            }
        }

        Ptr<WsnChannel> channel = CreateObject<WsnChannel>();
        NS_ABORT_MSG_IF(channel->AssignStreams(kChannelRngStream) != 1,
                        "WsnChannel RNG stream assignment consumed an unexpected stream count");
        channel->SetEnvironmentFactor(envFactor);
        channel->SetSignalLossPerMeterDbm(signalLossPerMDbm);

        RadioParameters radio; // nRF52840 defaults from Section 3 / 3.1
        radio.backgroundNoiseDbm = backgroundNoiseDbm;
        radio.boundEq17ToRMax = boundEq17ToRMax;
        radio.useNrf52840Energy = useNrf52840Energy;
        if (hfxoCrystal == "epson_fa128")
        {
            radio.hfxoStandbyCurrentMa = Nrf52840HfxoStandbyCurrentMa(Nrf52840HfxoCrystal::EPSON_FA128);
        }
        else if (hfxoCrystal == "epson_fa20h")
        {
            radio.hfxoStandbyCurrentMa = Nrf52840HfxoStandbyCurrentMa(Nrf52840HfxoCrystal::EPSON_FA20H);
        }
        else if (hfxoCrystal == "epson_tsx3225")
        {
            radio.hfxoStandbyCurrentMa = Nrf52840HfxoStandbyCurrentMa(Nrf52840HfxoCrystal::EPSON_TSX3225);
        }
        else if (hfxoCrystal == "ndk_nx1612aa")
        {
            radio.hfxoStandbyCurrentMa = Nrf52840HfxoStandbyCurrentMa(Nrf52840HfxoCrystal::NDK_NX1612AA);
        }
        else if (hfxoCrystal == "ndk_nx1210ab")
        {
            radio.hfxoStandbyCurrentMa = Nrf52840HfxoStandbyCurrentMa(Nrf52840HfxoCrystal::NDK_NX1210AB);
        }
        else if (hfxoCrystal != "none")
        {
            NS_FATAL_ERROR("Unknown --hfxoCrystal: " << hfxoCrystal);
        }
        // Keep the channel's noise floor and the metric-side noise floor
        // (Eq. 5/9) consistent -- they must represent the same physical
        // quantity. See the SNR-budget note in README.md.
        channel->SetBackgroundNoiseDbm(radio.backgroundNoiseDbm);

        if (interference && layout == "triangle" && !hypotenuseIds.empty())
        {
            // P0-3 fix: actually apply the interference variant (was a
            // documented no-op before). Penalize every link touching a
            // hypotenuse node, in both directions.
            for (uint32_t hid : hypotenuseIds)
            {
                for (uint32_t other = 0; other < nNodes; ++other)
                {
                    if (other == hid)
                    {
                        continue;
                    }
                    channel->SetLinkSnrPenaltyDb(hid, other, interferenceDb);
                    channel->SetLinkSnrPenaltyDb(other, hid, interferenceDb);
                }
            }
            if (run == 0)
            {
                NS_LOG_UNCOND("[run " << run << "] interference ENABLED: " << hypotenuseIds.size()
                                       << " hypotenuse node(s), penalty=" << interferenceDb << " dB");
            }
        }
        else if (interference && layout != "triangle" && run == 0)
        {
            NS_LOG_UNCOND("[run " << run
                                   << "] WARNING: --interference=true has no effect outside "
                                      "layout=triangle (the only layout the paper defines an "
                                      "interference variant for)");
        }

        std::vector<Ptr<WsnNetDevice>> devices;
        std::vector<Ptr<WsnRoutingApp>> apps;
        for (uint32_t i = 0; i < nNodes; ++i)
        {
            Ptr<WsnNetDevice> dev = CreateObject<WsnNetDevice>();
            dev->SetNodeId(i);
            dev->SetAddress(Mac48Address::Allocate());
            dev->SetChannel(channel);
            channel->Add(dev);
            devices.push_back(dev);

            Ptr<WsnRoutingApp> app = CreateObject<WsnRoutingApp>();
            bool isGateway = (i == 0);
            app->Configure(dev, i, isGateway, metricType, radio, AtpcMode::WITH_FEEDBACK);
            app->SetPingPayloadBytes(pingPayloadBytes);
            app->SetEmaAlpha(emaAlpha);
            app->SetDiscoveryWindowS(discoveryWindowS);
            app->SetDiscoveryTimeoutS(discoveryTimeoutS);
            app->SetMaxRelaysPerFlood(static_cast<uint8_t>(relayCap));
            app->SetTiming(1.0e-3,
                           1.0e-3,
                           static_cast<uint8_t>(macMaxRetries));
            app->SetAckTimeoutS(ackTimeoutS);
            app->SetPhyTiming(bitrateBps, radioOverheadS);
            nodes.Get(i)->AddApplication(app);
            app->SetStartTime(Seconds(0.0));
            apps.push_back(app);
        }

        // --- Connection phase: NET_SCAN + CONNECT + one path discovery
        // each (Section 4's "connection phase"; P1-4 fix gives NET_SCAN/
        // CONNECT genuine, separately-costed traffic instead of only
        // existing as unused enum values). Shifted by mobilityWarmupS so
        // the mobility process (if any) reaches steady state before any
        // measured phase begins -- see the mobility file-header note. ---
        NS_LOG_UNCOND("[run " << run << "] scheduling connection-phase for " << (nNodes - 1) << " nodes"
                               << (mobilityWarmupS > 0.0 ? " (after mobility warm-up)" : ""));
        for (uint32_t i = 1; i < nNodes; ++i)
        {
            Simulator::Schedule(Seconds(mobilityWarmupS + 0.01 * i), &WsnRoutingApp::StartConnectionPhase, apps[i], 0u);
        }

        // --- Ping phase: gateway pings every node 10x at 1s intervals ---
        std::vector<uint32_t> targets;
        for (uint32_t i = 1; i < nNodes; ++i)
        {
            targets.push_back(i);
        }
        double pingStart = mobilityWarmupS + 0.01 * nNodes + 1.0; // let the connection phase settle
        Simulator::Schedule(Seconds(pingStart), &WsnRoutingApp::StartPingCampaign, apps[0], targets);

        double stopTime = pingStart + 10.0 * targets.size() + 5.0;
        Simulator::Stop(Seconds(stopTime));

        // --- Mobility diagnostics (pre-flight validation, not part of the
        // measured experiment): periodically logs average node degree
        // (fraction of node pairs with PRR > 0.5) so a bounded random
        // walk's connectivity can be checked against its static baseline,
        // and a waypoint run's degree can be checked for collapse/blow-up
        // before committing to a full sweep. See README's validation
        // checklist. ---
        if (mobilityDiagnostics)
        {
            for (double t = 0.0; t <= stopTime; t += mobilityDiagnosticsPeriodS)
            {
                Simulator::Schedule(Seconds(t), [&, t]() {
                    uint32_t pairs = 0;
                    double degreeSum = 0.0;
                    for (uint32_t a = 0; a < nNodes; ++a)
                    {
                        uint32_t neighbors = 0;
                        Ptr<MobilityModel> ma = NodeList::GetNode(a)->GetObject<MobilityModel>();
                        for (uint32_t b = 0; b < nNodes; ++b)
                        {
                            if (a == b)
                            {
                                continue;
                            }
                            Ptr<MobilityModel> mb = NodeList::GetNode(b)->GetObject<MobilityModel>();
                            double d = ma->GetDistanceFrom(mb);
                            double lsl = LinkSignalLossDb(d, envFactor, signalLossPerMDbm);
                            double rssi = radio.pTxMaxDbm + lsl;
                            double snr = rssi - backgroundNoiseDbm;
                            if (PrrFromSnrDb(snr) > 0.5)
                            {
                                neighbors++;
                            }
                        }
                        degreeSum += neighbors;
                        pairs++;
                    }
                    double avgDegree = pairs > 0 ? degreeSum / pairs : 0.0;
                    diagCsv << layout << "," << envFactor << "," << metricStr << "," << mobility << "," << run << ","
                            << Simulator::Now().GetSeconds() << "," << avgDegree << "\n";
                });
            }
        }

        // Heartbeat: prints simulated time every 2 s of *simulated* time so
        // a hang can be localized to (a) never advancing past t=0 (a
        // zero-time event cascade / true infinite loop at a single
        // instant) vs (b) advancing slowly but validly (just a lot of
        // work).
        for (double t = 0.0; t < stopTime; t += 2.0)
        {
            Simulator::Schedule(Seconds(t), []() {
                NS_LOG_UNCOND("heartbeat t=" << Simulator::Now().GetSeconds() << "s");
            });
        }

        NS_LOG_UNCOND("[run " << run << "] Simulator::Run() starting, stopTime=" << stopTime << "s");
        Simulator::Run();
        NS_LOG_UNCOND("[run " << run << "] Simulator::Run() finished");

        double totalEnergy = 0.0;
        std::map<FrameType, double> energyByType;
        for (auto& app : apps)
        {
            const NodeStats& st = app->GetStats();
            totalEnergy += st.energyConsumedMWs;
            for (const auto& kv : st.energyByType)
            {
                energyByType[kv.first] += kv.second;
            }
        }
        uint32_t timeouts = apps[0]->GetStats().pingTimeouts;
        uint32_t noRoute = apps[0]->GetStats().pingNoRoute;
        uint32_t sent = apps[0]->GetStats().pingSent;

        csv << experimentId << "," << scenarioSet << "," << scenarioId << "," << manifestSha256 << ","
            << layout << "," << envFactor << "," << metricStr << "," << (interference ? 1 : 0) << ","
            << relayCap << "," << run << "," << seedBase << "," << (run + 1) << ","
            << nNodes << "," << spacingM << "," << signalLossPerMDbm << "," << backgroundNoiseDbm << ","
            << interferenceDb << "," << mobility << "," << mobilityBoxFrac << "," << mobilitySpeedMin << ","
            << mobilitySpeedMax << "," << mobilityPauseMin << "," << mobilityPauseMax << ","
            << mobilityWarmupS << "," << emaAlpha << "," << discoveryWindowS << "," << discoveryTimeoutS << ","
            << macMaxRetries << "," << ackTimeoutS << "," << bitrateBps << "," << radioOverheadS << ","
            << (useNrf52840Energy ? 1 : 0) << "," << hfxoCrystal << "," << (boundEq17ToRMax ? 1 : 0) << ","
            << pingPayloadBytes << ",with-feedback,"
            << totalEnergy << "," << timeouts << "," << noRoute << "," << sent;
        for (FrameType t : kAllFrameTypes)
        {
            auto it = energyByType.find(t);
            csv << "," << (it != energyByType.end() ? it->second : 0.0);
        }
        csv << "\n";

        if (routeOut.is_open())
        {
            const NodeStats& gatewayStats = apps[0]->GetStats();
            for (const auto& kv : gatewayStats.pingTrace)
            {
                const uint32_t targetId = kv.first.first;
                const uint32_t seq = kv.first.second;
                const PingTraceObservation& obs = kv.second;
                routeOut << experimentId << "," << scenarioSet << "," << scenarioId << ","
                         << manifestSha256 << "," << layout << "," << envFactor << "," << metricStr << ","
                         << (interference ? 1 : 0) << "," << relayCap << "," << run << "," << seedBase << ","
                         << (run + 1) << "," << targetId << "," << seq << "," << obs.status << ","
                         << obs.routeFingerprint << "," << obs.routeHopCount << "\n";
            }
        }

        Simulator::Destroy();
    }

    csv.close();
    if (routeOut.is_open())
    {
        routeOut.close();
    }
    return 0;
}

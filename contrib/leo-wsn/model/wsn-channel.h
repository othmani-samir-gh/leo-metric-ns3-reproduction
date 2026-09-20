/*
 * wsn-channel.h
 *
 * A lightweight, application-level wireless channel abstraction used to
 * reproduce the *simulation methodology* of Section 4 of the paper
 * (MeshProtocolSimulator-style topologies, Fig. 4) inside ns-3, without
 * depending on a specific PHY/MAC module that models the exact nRF52840
 * radio (ns-3 has no native nRF52840/BLE-mesh PHY).
 *
 * Two paper equations are reproduced directly:
 *   Eq. (18)  LSL_AB = -10 * N * log10(l_AB) + P_l      (path loss)
 *   Fig. 2    empirical PRR(SNR) / PER(SNR) curve, digitized as a
 *             monotonic lookup table (see kPrrCurve below).
 *
 * PROVENANCE (updated Sept 2026): kPrrCurve is now digitized directly
 * from the paper's Fig. 2 "OK" (successful reception) curve, extracted by
 * the user via WebPlotDigitizer (automeris.io/wpd/) -- 95 points spanning
 * SNR ~1.25-77.8 dB, i.e. essentially the paper's full measured range.
 * This replaces an earlier revision that was only a monotonic
 * interpolation of the qualitative landmarks stated in the paper's text
 * (PRR>90% for SNR in [+24,+78] dB, sharp drop below +24 dB, no
 * reception at/below +5 dB). It is still not a substitute for the
 * authors' own raw measurement data (which remain unpublished): any
 * digitization from a static image chart carries inherent pixel-level
 * reading error, and this is one independent digitization, not a
 * cross-validated one. Report it as "digitized from the published
 * figure via WebPlotDigitizer" rather than as the authors' original data
 * if you cite it. See the detailed provenance note at kPrrCurve's
 * definition in wsn-channel.cc, including a self-consistency check
 * against the paper's companion "No reception" and "CRC error" curves
 * (also digitized, summing to ~100% with the OK curve at matching SNR
 * values as they must for three mutually exclusive outcomes).
 */
#ifndef LEO_WSN_CHANNEL_H
#define LEO_WSN_CHANNEL_H

#include <ns3/channel.h>
#include <ns3/mobility-model.h>
#include <ns3/packet.h>
#include <ns3/random-variable-stream.h>

#include <map>
#include <utility>
#include <vector>

namespace ns3
{
namespace leo
{

class WsnNetDevice;

/// One (SNR[dB], PRR[0-1]) control point for linear interpolation.
struct PrrPoint
{
    double snrDb;
    double prr;
};

/**
 * \brief Monotonic PRR(SNR) table reproducing the qualitative landmarks of
 * Fig. 2 (see class-level comment for the caveat about its provenance).
 */
extern const std::vector<PrrPoint> kPrrCurve;

/// Piecewise-linear interpolation of kPrrCurve; clamps outside the range.
double PrrFromSnrDb(double snrDb);

/**
 * \brief Path loss per Eq. (18): LSL_AB = -10*N*log10(l_AB) + P_l
 *
 * \param distanceM         l_AB, link distance in meters (must be > 0)
 * \param environmentFactor N, the paper's environmental factor (2..3 in
 *                           Section 4)
 * \param signalLossPerMDbm P_l, always negative; the paper measured -64 dBm
 */
double LinkSignalLossDb(double distanceM, double environmentFactor, double signalLossPerMDbm);

/**
 * \brief Simple shared-medium channel connecting several WsnNetDevice
 * instances. On Send(), the channel computes the received SNR at every
 * other attached device from node positions (Eq. 18) and background
 * noise, looks up the PRR (Fig. 2), and stochastically decides
 * success/failure per receiver -- i.e. per link, matching the paper's
 * link-level PRR/PER framing.
 */
class WsnChannel : public Channel
{
  public:
    static TypeId GetTypeId();
    WsnChannel();

    void Add(Ptr<WsnNetDevice> dev);

    std::size_t GetNDevices() const override;
    Ptr<NetDevice> GetDevice(std::size_t i) const override;

    /**
     * \brief Called by a WsnNetDevice to broadcast \p packet at
     * \p txPowerDbm to EVERY attached device (used for NET_SCAN/CONNECT/
     * PATH_DISCOVERY, which are genuinely meant for every listener).
     */
    void Send(Ptr<WsnNetDevice> sender, Ptr<Packet> packet, double txPowerDbm);

    /**
     * \brief Logical point-to-point delivery within the shared wireless
     * medium: computes distance -> LSL -> SNR -> PRR (including any
     * configured interference penalty) for the SINGLE (sender,
     * intendedNextHopId) link only, and rolls delivery just once for that
     * link -- instead of Send()'s behaviour of evaluating and
     * potentially scheduling delivery to every attached device. Used for
     * genuinely point-to-point traffic (PING/PING_REPLY/
     * PATH_DISCOVERY_REPLY/ACK, i.e. everything sent via
     * WsnRoutingApp::SendUnicastReliable). This does not change the
     * underlying physical model at all (the same distance/LSL/SNR/PRR/
     * interference pipeline is used) -- it only avoids computing and
     * rolling delivery for devices that were never the intended
     * recipient, which the `intendedNextHopId` guard in
     * WsnRoutingApp::OnReceive would have rejected anyway. Kept as a
     * separate method (rather than folding into Send()) so the broadcast
     * path used by discovery/scan traffic is untouched.
     */
    void SendUnicast(Ptr<WsnNetDevice> sender, Ptr<Packet> packet, double txPowerDbm, uint32_t intendedNextHopId);

    void SetEnvironmentFactor(double n)
    {
        m_environmentFactor = n;
    }

    void SetSignalLossPerMeterDbm(double pl)
    {
        m_signalLossPerMeterDbm = pl;
    }

    void SetBackgroundNoiseDbm(double n)
    {
        m_backgroundNoiseDbm = n;
    }

    double GetBackgroundNoiseDbm() const
    {
        return m_backgroundNoiseDbm;
    }

    /**
     * \brief Apply an extra SNR penalty (in dB, subtracted from the
     * computed SNR before the PRR lookup) to a specific ordered node-id
     * pair, implementing the paper's "triangle-with-interference" variant
     * (Section 4: hypotenuse nodes have degraded PRR to simulate
     * interference). Call once per direction you want penalized; if only
     * one direction is set the link is asymmetric, matching how
     * interference affecting one node's receiver need not affect the
     * other's.
     */
    void SetLinkSnrPenaltyDb(uint32_t nodeIdA, uint32_t nodeIdB, double penaltyDb);

    /// Clears all penalties set via SetLinkSnrPenaltyDb.
    void ClearLinkSnrPenalties();

  private:
    /// Shared delivery-evaluation logic used by both Send() (looped over
    /// every device) and SendUnicast() (called once for the intended
    /// recipient only): computes distance -> LSL -> SNR -> PRR (with any
    /// configured interference penalty) for the (sender, receiver) pair
    /// and stochastically decides delivery, scheduling
    /// WsnNetDevice::Receive on success.
    void DeliverIfSuccessful(Ptr<WsnNetDevice> sender, Ptr<WsnNetDevice> receiver, Ptr<Packet> packet, double txPowerDbm);

    std::vector<Ptr<WsnNetDevice>> m_devices;
    double m_environmentFactor{2.0};
    double m_signalLossPerMeterDbm{-64.0}; //!< as measured by the authors, Section 4
    // Default kept in sync with RadioParameters::backgroundNoiseDbm
    // (route-metrics.h); scratch/leo-topologies.cc re-asserts this
    // explicitly via SetBackgroundNoiseDbm() so the two never drift apart.
    double m_backgroundNoiseDbm{-116.0};
    Ptr<UniformRandomVariable> m_rng;
    std::map<std::pair<uint32_t, uint32_t>, double> m_linkSnrPenaltyDb;
};

} // namespace leo
} // namespace ns3

#endif // LEO_WSN_CHANNEL_H

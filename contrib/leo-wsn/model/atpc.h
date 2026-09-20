/*
 * atpc.h
 *
 * Implements the Automatic Transmission Power Control (ATPC) procedures of
 * Section 3.2, i.e. Algorithm 1 (pre-transmission) and Algorithm 2
 * (post-reception), plus the supporting equations:
 *   Eq. (6)  LSL   = RSSI_m - P_TX - G_RX
 *   Eq. (7)  P_TX,adj = SNR_th - SNR
 *   Eq. (8)  SNR_th   = SNR_s + 5 dB
 *   Eq. (9)  P_th     = SNR_th + N + G_RX
 *   Eq. (10) P_TX,R   = P_th - LSL
 */
#ifndef LEO_ATPC_H
#define LEO_ATPC_H

#include "route-metrics.h" // for RadioParameters

namespace ns3
{
namespace leo
{

enum class AtpcMode
{
    WITH_FEEDBACK,   //!< Fig. 3(a)/(b): adjustment carried in ACK / next data frame
    WITHOUT_FEEDBACK //!< Fig. 3(c): P_th / LSL based, Eq. (9)-(10)
};

/// Eq. (6)
inline double
ComputeLinkSignalLossDb(double rssiMeasuredDbm, double pTxUsedDbm, double rxAntennaGainDb)
{
    return rssiMeasuredDbm - pTxUsedDbm - rxAntennaGainDb;
}

/// Eq. (8)
inline double
ComputeSnrThresholdDb(const RadioParameters& radio)
{
    return radio.snrSensitivityDb + radio.atpcOffsetDb;
}

/// Eq. (7): call at the receiver right after measuring SNR of an incoming frame.
inline double
ComputeTxPowerAdjustmentDb(double measuredSnrDb, const RadioParameters& radio)
{
    return ComputeSnrThresholdDb(radio) - measuredSnrDb;
}

/// Eq. (9): threshold RSSI/power the *sender of P_th* requires for reliable reception.
inline double
ComputeRequiredReceivePowerDbm(const RadioParameters& radio, double rxAntennaGainDb)
{
    return ComputeSnrThresholdDb(radio) + radio.backgroundNoiseDbm + rxAntennaGainDb;
}

/// Eq. (10): required TX power for the ACK, given peer's P_th and measured LSL.
inline double
ComputeRequiredTxPowerFromLsl(double peerPThDbm, double linkSignalLossDb)
{
    return peerPThDbm - linkSignalLossDb;
}

/**
 * \brief Algorithm 1 - compute the value to embed in the header of an
 * outgoing frame, *before* transmission.
 *
 * \param mode            feedback or no-feedback ATPC
 * \param measuredSnrDb   only used in WITH_FEEDBACK mode (ignored otherwise)
 * \param radio           radio/deployment constants
 * \param rxAntennaGainDb G_RX, only used in WITHOUT_FEEDBACK mode
 * \return either P_TX,adj (feedback) or P_th (no feedback), matching what
 *         Algorithm 1 places in the frame header.
 */
double AlgorithmOnePreTransmission(AtpcMode mode,
                                    double measuredSnrDb,
                                    const RadioParameters& radio,
                                    double rxAntennaGainDb);

/**
 * \brief Algorithm 2 - compute the *Required TXP* (ReqTXP) to store in the
 * Neighbor Table after receiving a frame.
 *
 * \param wasRetransmitted     true if the received frame's "retransmitted"
 *                              flag was set -> ReqTXP snaps to P_TX,max
 * \param mode                  feedback or no-feedback ATPC
 * \param prevReqTxPowerDbm     previous ReqTXP (P_TX,R,prev); ignored if !prevKnown
 * \param prevKnown             whether ReqTXP was previously known
 * \param pTxAdjDb              P_TX,adj received in header (feedback mode)
 * \param pThDbm                P_th received in header (no-feedback mode)
 * \param linkSignalLossDb      LSL computed for this reception, Eq. (6)
 * \param radio                 radio/deployment constants
 */
double AlgorithmTwoPostReception(bool wasRetransmitted,
                                  AtpcMode mode,
                                  double prevReqTxPowerDbm,
                                  bool prevKnown,
                                  double pTxAdjDb,
                                  double pThDbm,
                                  double linkSignalLossDb,
                                  const RadioParameters& radio);

} // namespace leo
} // namespace ns3

#endif // LEO_ATPC_H

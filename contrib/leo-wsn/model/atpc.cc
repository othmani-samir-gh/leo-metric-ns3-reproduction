#include "atpc.h"

namespace ns3
{
namespace leo
{

double
AlgorithmOnePreTransmission(AtpcMode mode,
                             double measuredSnrDb,
                             const RadioParameters& radio,
                             double rxAntennaGainDb)
{
    if (mode == AtpcMode::WITH_FEEDBACK)
    {
        // P_TX,adj <- SNR_th - SNR
        return ComputeTxPowerAdjustmentDb(measuredSnrDb, radio);
    }
    // P_th <- SNR_th + N + G_RX
    return ComputeRequiredReceivePowerDbm(radio, rxAntennaGainDb);
}

double
AlgorithmTwoPostReception(bool wasRetransmitted,
                           AtpcMode mode,
                           double prevReqTxPowerDbm,
                           bool prevKnown,
                           double pTxAdjDb,
                           double pThDbm,
                           double linkSignalLossDb,
                           const RadioParameters& radio)
{
    if (wasRetransmitted)
    {
        // P_TX,R <- P_TX,max
        return radio.pTxMaxDbm;
    }

    if (mode == AtpcMode::WITH_FEEDBACK)
    {
        if (!prevKnown || prevReqTxPowerDbm == radio.pTxMaxDbm)
        {
            return radio.pTxMaxDbm + pTxAdjDb;
        }
        return prevReqTxPowerDbm + pTxAdjDb;
    }

    // WITHOUT_FEEDBACK branch, Algorithm 2 else-branch:
    double pTxRTmp = ComputeRequiredTxPowerFromLsl(pThDbm, linkSignalLossDb); // Eq. (10)

    if (!prevKnown || prevReqTxPowerDbm < (pTxRTmp + 3.0))
    {
        // "recommended to increase ... by at least +3 dB more"
        return pTxRTmp + 3.0;
    }
    if (prevReqTxPowerDbm > (pTxRTmp + 5.0))
    {
        // "Transmission power can be decreased only when it is decreased
        // by more than 5 dB. After that, ... increased by +5 dB to avoid
        // possible errors."
        return pTxRTmp + 5.0;
    }
    return prevReqTxPowerDbm;
}

} // namespace leo
} // namespace ns3

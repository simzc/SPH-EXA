/*
 * MIT License
 *
 * Copyright (c) 2021 CSCS, ETH Zurich
 *               2021 University of Basel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*! @file
 * @brief Min-reduction to determine global timestep
 *
 * @author Sebastian Keller <sebastian.f.keller@gmail.com>
 * @author Aurelien Cavelan
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include <mpi.h>

#include "cstone/primitives/primitives_gpu.h"
#include "sph/kernels.hpp"
#include "sph/timestep.h"

namespace sph
{

//! @brief compute Divv-limited timestep for each group when block time-steps are active
template<class Dataset>
void groupDivvTimestep(const GroupView& grp, float* groupDt, const Dataset& d)
{
    if constexpr (cstone::HaveGpu<typename Dataset::AcceleratorType>{})
    {
        groupDivvTimestepGpu(d.Krho, grp, rawPtr(d.devData.divv), groupDt);
    }
}

//! @brief compute acceleration-limited timestep for each group when block time-steps are active
template<class Dataset>
void groupAccTimestep(const GroupView& grp, float* groupDt, const Dataset& d)
{
    if constexpr (cstone::HaveGpu<typename Dataset::AcceleratorType>{})
    {
        groupAccTimestepGpu(d.etaAcc * std::sqrt(d.eps), grp, rawPtr(d.devData.ax), rawPtr(d.devData.ay),
                            rawPtr(d.devData.az), groupDt);
    }
}

//! @brief sort groupDt, keeping track of the ordering
template<class AccVec>
void sortGroupDt(float* groupDt, cstone::LocalIndex* groupIndices, cstone::LocalIndex numGroups, AccVec& scratch)
{
    using cstone::LocalIndex;
    size_t oldSize  = reallocateBytes(scratch, (sizeof(float) + sizeof(LocalIndex)) * numGroups);
    auto*  keyBuf   = reinterpret_cast<float*>(rawPtr(scratch));
    auto*  valueBuf = reinterpret_cast<LocalIndex*>(keyBuf + numGroups);
    cstone::sequenceGpu(groupIndices, numGroups, 0u);
    cstone::sortByKeyGpu(groupDt, groupDt + numGroups, groupIndices, keyBuf, valueBuf);
    reallocate(oldSize, scratch);
};

//! @brief return the local minimum timestep and the biggest timestep of the fastest fraction of particles
inline auto timestepRangeGpu(const float* groupDt, cstone::LocalIndex numGroups, float fastFraction)
{
    std::array<float, 2> minGroupDt;
    memcpyD2H(groupDt, 1, minGroupDt.data());
    memcpyD2H(groupDt + cstone::LocalIndex(fastFraction * numGroups), 1, minGroupDt.data() + 1);
    return minGroupDt;
}

//! @brief Determine timestep rungs
template<class AccVec>
Timestep computeRungTimestep(const GroupView& grp, float* groupDt, cstone::LocalIndex* groupIndices, AccVec& scratch)
{
    using cstone::LocalIndex;

    std::array<float, 2> minGroupDt;
    if constexpr (IsDeviceVector<AccVec>{})
    {
        sortGroupDt(groupDt, groupIndices, grp.numGroups, scratch);
        minGroupDt = timestepRangeGpu(groupDt, grp.numGroups, 0.4);
    }

    std::array<float, 2> minDtGlobal;
    mpiAllreduce(minGroupDt.data(), minDtGlobal.data(), minGroupDt.size(), MPI_MIN);

    int numRungs = std::min(int(log2(minDtGlobal[1] / minDtGlobal[0])) + 1, Timestep::maxNumRungs);

    // find ranges of 2*minDt, 4*minDt, 8*minDt
    // groupDt is sorted, groups belonging to a specific rung will correspond to index ranges
    std::array<LocalIndex, Timestep::maxNumRungs + 1> rungRanges{0};
    std::fill(rungRanges.begin() + 1, rungRanges.end(), grp.numGroups);
    if constexpr (IsDeviceVector<AccVec>{})
    {
        for (int rung = 1; rung < numRungs; ++rung)
        {
            float maxDtRung  = (1 << rung) * minDtGlobal[0];
            rungRanges[rung] = cstone::lowerBoundGpu(groupDt, groupDt + grp.numGroups, maxDtRung);
        }
    }

    Timestep ret{.minDt = minDtGlobal[0], .numRungs = numRungs, .substep = 0, .rungRanges = rungRanges};
    std::fill(ret.dt_drift.begin(), ret.dt_drift.end(), 0);
    return ret;
}

} // namespace sph

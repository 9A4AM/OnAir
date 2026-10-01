#include "dect2/ldpc.h"
#include <algorithm>
#include <cmath>

namespace dect2 {

LdpcCode::LdpcCode(int k, int n, const std::vector<std::vector<int>>& rows) : k_(k), n_(n), m_(n - k) {
    const int q = m_ / 360;
    std::vector<std::vector<int>> chk(m_);
    int im = 0;
    for (auto& row : rows) {
        for (int c = 0; c < 360; c++, im++) {
            for (int a : row) {
                int p = (a + c * q) % m_;
                encP_.push_back(p);
                encD_.push_back(im);
                chk[p].push_back(im);
            }
        }
    }
    // zig-zag parity structure: check j involves parity bits j and j-1
    for (int j = 0; j < m_; j++) {
        chk[j].push_back(k_ + j);
        if (j > 0) chk[j].push_back(k_ + j - 1);
    }
    chkStart_.resize(m_ + 1);
    chkStart_[0] = 0;
    for (int j = 0; j < m_; j++) chkStart_[j + 1] = chkStart_[j] + (int)chk[j].size();
    chkVar_.reserve(chkStart_[m_]);
    for (int j = 0; j < m_; j++) chkVar_.insert(chkVar_.end(), chk[j].begin(), chk[j].end());
}

void LdpcCode::encode(std::vector<uint8_t>& bits) const {
    bits.resize(n_);
    std::fill(bits.begin() + k_, bits.end(), 0);
    for (size_t e = 0; e < encP_.size(); e++) bits[k_ + encP_[e]] ^= bits[encD_[e]];
    for (int j = 1; j < m_; j++) bits[k_ + j] ^= bits[k_ + j - 1];
}

bool LdpcCode::decode(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters) const {
    const int E = chkStart_[m_];
    std::vector<float> c2v(E, 0.f), v2c(E, 0.f), total(n_);
    hard.assign(n_, 0);
    const float alpha = 0.8f; // normalisation factor
    int it = 0;
    bool ok = false;
    for (; it < maxIter; it++) {
        // variable -> check: total LLR minus the check's own contribution
        std::copy(llr.begin(), llr.end(), total.begin());
        for (int e = 0; e < E; e++) total[chkVar_[e]] += c2v[e];
        for (int e = 0; e < E; e++) v2c[e] = total[chkVar_[e]] - c2v[e];
        // check update (min-sum)
        for (int j = 0; j < m_; j++) {
            int s = chkStart_[j], t = chkStart_[j + 1];
            float min1 = 1e30f, min2 = 1e30f;
            int idx1 = -1;
            int sign = 0;
            for (int e = s; e < t; e++) {
                float a = std::fabs(v2c[e]);
                if (v2c[e] < 0) sign ^= 1;
                if (a < min1) { min2 = min1; min1 = a; idx1 = e; }
                else if (a < min2) min2 = a;
            }
            for (int e = s; e < t; e++) {
                float mag = (e == idx1 ? min2 : min1) * alpha;
                int sg = sign ^ (v2c[e] < 0 ? 1 : 0);
                c2v[e] = sg ? -mag : mag;
            }
        }
        // decisions and syndrome
        std::copy(llr.begin(), llr.end(), total.begin());
        for (int e = 0; e < E; e++) total[chkVar_[e]] += c2v[e];
        for (int i = 0; i < n_; i++) hard[i] = total[i] < 0;
        ok = true;
        for (int j = 0; j < m_ && ok; j++) {
            int p = 0;
            for (int e = chkStart_[j]; e < chkStart_[j + 1]; e++) p ^= hard[chkVar_[e]];
            if (p) ok = false;
        }
        if (ok) { it++; break; }
    }
    if (iters) *iters = it;
    return ok;
}

} // namespace dect2

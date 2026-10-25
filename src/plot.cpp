#include <NeFlux/common.hpp>
#include <NeFlux/plot.hpp>
namespace neforce::neflux {

    size_t plot_window::stride(size_t width) const {
        return neforce::max(static_cast<size_t>(1),
                            (samples + width / 2) / neforce::max(static_cast<size_t>(1), width));
    }

    size_t plot_window::capacity(size_t width) const { return neforce::min(samples, width * stride(width)); }

    void plot_window::project(const deque<double>& values, size_t width, vector<plot_range>& out) const {
        out.clear();
        out.resize(width);
        if (width == 0 || values.empty()) {
            return;
        }
        if (has_time) {
            const auto& times = *time.timestamps;
            const auto& periods = *time.periods;
            if (times.empty()) {
                return;
            }
            const uint64_t step = neforce::max(
                    static_cast<uint64_t>(1),
                    (neforce::max(static_cast<uint64_t>(1), time.seconds) * 1'000'000 + width - 1) / width);
            const auto bucket = [step](double t) {
                return static_cast<uint64_t>(neforce::round(neforce::max(0.0, t) * 1'000'000.0)) / step;
            };
            const uint64_t end = bucket(times.back());
            const size_t count = neforce::min(values.size(), times.size());
            deque<bool> gaps(width, false);
            vector<bool> occupied(width, false);
            bool has_previous = false;
            int64_t previous_x = 0;
            double previous_value = 0.0;
            double previous_time = 0.0;
            double previous_period = 1.0;
            for (size_t i = 0; i < count; ++i) {
                const size_t index = times.size() - count + i;
                const double t = times[index];
                const double period = index < periods.size() ? neforce::max(0.001, periods[index]) : 1.0;
                const double value = values[values.size() - count + i];
                const int64_t x = static_cast<int64_t>(width) - 1 - static_cast<int64_t>(end - bucket(t));
                if (!neforce::is_finite(value)) {
                    has_previous = false;
                    if (x >= 0) {
                        gaps[static_cast<size_t>(x)] = true;
                        out[static_cast<size_t>(x)].gap = true;
                    }
                    continue;
                }
                if (has_previous && x > previous_x &&
                    t - previous_time <= 2.5 * neforce::max(previous_period, period)) {
                    for (int64_t col = neforce::max<int64_t>(previous_x + 1, 0); col < x; ++col) {
                        const double interpolated = previous_value + (value - previous_value) *
                                                                             static_cast<double>(col - previous_x) /
                                                                             static_cast<double>(x - previous_x);
                        plot_range range;
                        range.low = interpolated;
                        range.high = interpolated;
                        range.last = interpolated;
                        range.gap = gaps[static_cast<size_t>(col)];
                        out[static_cast<size_t>(col)] = range;
                        occupied[static_cast<size_t>(col)] = true;
                    }
                }
                if (x >= 0) {
                    auto& range = out[static_cast<size_t>(x)];
                    if (!occupied[static_cast<size_t>(x)]) {
                        range = plot_range{value, value, value, gaps[static_cast<size_t>(x)]};
                        occupied[static_cast<size_t>(x)] = true;
                    } else {
                        range.low = neforce::min(range.low, value);
                        range.high = neforce::max(range.high, value);
                        range.last = value;
                    }
                }
                has_previous = true;
                previous_x = x;
                previous_value = value;
                previous_time = t;
                previous_period = period;
            }
            return;
        }
        const size_t stride_value = stride(width);
        const uint64_t end = neforce::max<uint64_t>(end, values.size());
        const uint64_t last_bucket = (end - 1) / stride_value;
        const uint64_t start = end - values.size();
        const size_t skip = values.size() > samples ? values.size() - samples : 0;
        deque<bool> gaps(width, false);
        vector<bool> occupied(width, false);
        for (size_t i = skip; i < values.size(); ++i) {
            const uint64_t offset = last_bucket - (start + i) / stride_value;
            if (offset >= width) {
                continue;
            }
            const size_t x = width - 1 - static_cast<size_t>(offset);
            const double value = values[i];
            if (!neforce::is_finite(value)) {
                gaps[x] = true;
                out[x].gap = true;
            } else {
                auto& range = out[x];
                if (!occupied[x]) {
                    range = plot_range{value, value, value, gaps[x]};
                    occupied[x] = true;
                } else {
                    range.low = neforce::min(range.low, value);
                    range.high = neforce::max(range.high, value);
                    range.last = value;
                }
            }
        }
    }

    double nice_max(double value) {
        if (!neforce::is_finite(value) || value <= 1.0) {
            return 1.0;
        }
        double scale = 1.0;
        while (scale * 10.0 <= value) {
            scale *= 10.0;
        }
        const double fraction = value / scale;
        const double step = fraction <= 1.0 ? 1.0 : (fraction <= 2.0 ? 2.0 : (fraction <= 5.0 ? 5.0 : 10.0));
        return neforce::max(1.0, scale * step);
    }


} // namespace neforce::neflux

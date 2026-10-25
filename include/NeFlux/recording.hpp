#ifndef NEXUSFLUX_RECORDING_HPP__
#define NEXUSFLUX_RECORDING_HPP__

/**
 * @file recording.hpp
 * @brief 采样录制与回放
 */

#include <NeFlux/model.hpp>
namespace neforce::neflux {

    /**
     * @brief 快照录制器
     *
     * 以 NDJSON 追加写入，并按配置大小轮转当前文件与三个历史文件。
     */
    class recorder {
    public:
        /**
         * @brief 打开录制文件
         * @param file_path 路径
         * @param max_mib 分卷大小（MiB）
         * @throws file_exception 无法打开时抛出
         */
        recorder(const string& file_path, uint64_t max_mib);

        /**
         * @brief 写入一份快照
         * @param value 快照
         * @throws file_exception 写入失败时抛出
         */
        void write(const snapshot& value);

        /**
         * @brief 获取已写字节数
         * @return 当前文件大小
         */
        NEFORCE_NODISCARD uint64_t size() const noexcept { return size_; }

    private:
        string path_;
        uint64_t max_bytes_ = 0;
        uint64_t size_ = 0;
    };

    /**
     * @brief 读取录制文件
     * @param file_path 路径
     * @return 快照列表
     * @throws value_exception 内容非法或文件过大时抛出
     */
    NEFORCE_NODISCARD vector<snapshot> replay(const string& file_path);

    /**
     * @brief 将一份快照展开为 CSV 行
     * @param value 快照
     * @param header 是否写入表头
     * @return CSV 文本
     */
    NEFORCE_NODISCARD string snapshot_csv(const snapshot& value, bool header);

} // namespace neforce::neflux
#endif // NEXUSFLUX_RECORDING_HPP__

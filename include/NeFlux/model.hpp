#ifndef NEXUSFLUX_MODEL_HPP__
#define NEXUSFLUX_MODEL_HPP__

/**
 * @file model.hpp
 * @brief 快照数据模型
 */

#include <NeFlux/json.hpp>
#include <NeForce/core/container/vector.hpp>
#include <NeForce/core/string/string.hpp>
#include <NeForce/core/utility/optional.hpp>
namespace neforce::neflux {

    /**
     * @brief 工作负载标签
     *
     * 标签来自采样证据或程序类别推断，用于识别软件与用途。
     */
    struct tag {
        string key_;              ///< 稳定标识
        string label_;            ///< 展示名称
        bool inferred_ = false;   ///< 是否为用途推断
        vector<string> evidence_; ///< 证据列表

        /**
         * @brief 获取展示名称
         * @return 推断标签带 `?` 后缀
         */
        NEFORCE_NODISCARD string display() const;
    };

    // TODO: I think you should move these sample to neforce::sysinfo or neforce::process

    /**
     * @brief 内存概览
     */
    struct memory {
        uint64_t total_ = 0;        ///< 物理内存总量（字节）
        uint64_t used_ = 0;         ///< 已用内存（字节）
        uint64_t available_ = 0;    ///< 可用内存（字节）
        optional<uint64_t> free_;   ///< 完全空闲内存（字节）
        optional<uint64_t> cached_; ///< 页缓存与可回收 slab（字节）
        uint64_t swap_total_ = 0;   ///< 交换区总量（字节）
        uint64_t swap_used_ = 0;    ///< 已用交换区（字节）
    };

    /**
     * @brief 进程采样
     */
    struct process {
        vector<tag> tags_;               ///< 工作负载标签
        uint32_t pid_ = 0;               ///< 进程标识
        optional<uint32_t> parent_;      ///< 父进程标识
        uint64_t start_time_ = 0;        ///< 启动时间（UNIX 秒）
        string name_;                    ///< 进程名
        string command_;                 ///< 完整命令行
        string user_;                    ///< 所属用户
        string state_ = "Unknown";       ///< 调度状态
        float cpu_ = 0.0F;               ///< CPU 占用，单核满载为 100%
        uint64_t memory_ = 0;            ///< 常驻内存（字节）
        uint64_t virtual_memory_ = 0;    ///< 虚拟内存（字节）
        uint64_t cpu_time_ms_ = 0;       ///< 累计 CPU 时间（毫秒）
        double read_per_sec_ = 0.0;      ///< 读取速率（字节/秒）
        double write_per_sec_ = 0.0;     ///< 写入速率（字节/秒）
        uint64_t read_total_ = 0;        ///< 累计读取（字节）
        uint64_t write_total_ = 0;       ///< 累计写入（字节）
        optional<uint64_t> gpu_memory_;  ///< GPU 显存占用（字节）
        optional<uint32_t> gpu_percent_; ///< GPU 利用率
        vector<uint32_t> gpu_ids_;       ///< 关联 GPU 编号
        uint64_t runtime_ = 0;           ///< 运行时长（秒）
        bool thread_ = false;            ///< 是否为线程
        bool kernel_thread_ = false;     ///< 是否为内核线程
        optional<int32_t> nice_;         ///< nice 值
        optional<double> rx_per_sec_;    ///< 网络接收速率（字节/秒）
        optional<double> tx_per_sec_;    ///< 网络发送速率（字节/秒）
        json extra_;                     ///< 扩展字段
    };

    /**
     * @brief 网络接口采样
     */
    struct network {
        string name_;                         ///< 接口名
        string mac_;                          ///< MAC 地址
        uint64_t mtu_ = 0;                    ///< MTU
        optional<uint64_t> speed_mbps_;       ///< 链路速率（Mbit/s）
        uint64_t rx_packets_ = 0;             ///< 累计接收包
        uint64_t tx_packets_ = 0;             ///< 累计发送包
        optional<double> rx_packets_per_sec_; ///< 接收包速率
        optional<double> tx_packets_per_sec_; ///< 发送包速率
        uint64_t rx_errors_ = 0;              ///< 接收错误
        uint64_t tx_errors_ = 0;              ///< 发送错误
        double rx_per_sec_ = 0.0;             ///< 接收速率（字节/秒）
        double tx_per_sec_ = 0.0;             ///< 发送速率（字节/秒）
        uint64_t rx_total_ = 0;               ///< 累计接收（字节）
        uint64_t tx_total_ = 0;               ///< 累计发送（字节）
        uint64_t errors_ = 0;                 ///< 错误总数
        optional<uint64_t> drops_;            ///< 丢包数
        string state_;                        ///< 运行状态
        string address_;                      ///< IP 地址列表
    };

    /**
     * @brief 文件系统采样
     */
    struct disk {
        string name_;            ///< 设备名
        string mount_;           ///< 挂载点
        string filesystem_;      ///< 文件系统类型
        string kind_;            ///< 介质类型
        bool removable_ = false; ///< 是否可移动
        uint64_t total_ = 0;     ///< 容量（字节）
        uint64_t used_ = 0;      ///< 已用（字节）
        uint64_t available_ = 0; ///< 可用（字节）
        json extra_;             ///< 扩展字段
    };

    /**
     * @brief 块设备 I/O 采样
     */
    struct disk_io {
        string name_;                   ///< 设备名
        double read_per_sec_ = 0.0;     ///< 读取速率（字节/秒）
        double write_per_sec_ = 0.0;    ///< 写入速率（字节/秒）
        optional<double> busy_percent_; ///< 忙碌百分比
        bool aggregate_ = false;        ///< 是否为整盘设备
        optional<double> read_iops_;    ///< 读 IOPS
        optional<double> write_iops_;   ///< 写 IOPS
        optional<double> await_ms_;     ///< 平均等待（毫秒）
        optional<double> queue_depth_;  ///< 队列深度
    };

    /**
     * @brief 温度传感器采样
     */
    struct sensor {
        string name_;              ///< 传感器名称
        float temperature_ = 0.0F; ///< 摄氏度
        optional<float> critical_; ///< 临界温度（摄氏度）
    };

    /**
     * @brief 电池采样
     */
    struct battery {
        string name_;             ///< 电池名称
        json extra_;              ///< 扩展字段
        optional<float> percent_; ///< 剩余电量百分比
        string status_;           ///< 充电状态
        optional<float> watts_;   ///< 充放电功率（瓦）

        /**
         * @brief 获取通俗的充电状态
         * @return 状态文本
         */
        NEFORCE_NODISCARD string state_label() const;

        /**
         * @brief 获取剩余时间
         * @return 形如 `0h41m` 的文本，未知时为空
         */
        NEFORCE_NODISCARD optional<string> remaining() const;

        /**
         * @brief 获取单行摘要
         * @return 形如 `87% · charging · 0h41m to full · 31.2 W`
         */
        NEFORCE_NODISCARD string summary() const;
    };

    /**
     * @brief 系统压力采样
     */
    struct pressure {
        string resource_;                  ///< 资源类型
        optional<double> some_10_;         ///< some avg10
        optional<double> some_60_;         ///< some avg60
        optional<double> some_300_;        ///< some avg300
        optional<uint64_t> some_total_us_; ///< some total
        optional<double> full_10_;         ///< full avg10
        optional<double> full_60_;         ///< full avg60
        optional<double> full_300_;        ///< full avg300
        optional<uint64_t> full_total_us_; ///< full total
    };

    /**
     * @brief GPU 进程采样
     */
    struct gpu_process {
        uint32_t pid_ = 0;                      ///< 进程标识
        optional<uint64_t> memory_;             ///< 显存占用（字节）
        optional<uint32_t> utilization_;        ///< 计算利用率
        string kind_;                           ///< 进程类型
        optional<uint32_t> memory_utilization_; ///< 显存带宽利用率
        optional<uint32_t> encoder_;            ///< 编码利用率
        optional<uint32_t> decoder_;            ///< 解码利用率
    };

    /**
     * @brief GPU 设备采样
     */
    struct gpu {
        uint32_t id_ = 0;                       ///< 设备编号
        string name_;                           ///< 设备名称
        string vendor_;                         ///< 厂商
        string backend_;                        ///< 数据后端
        string uuid_;                           ///< 设备 UUID
        string driver_;                         ///< 驱动版本
        optional<string> cuda_driver_version_;  ///< CUDA Driver API 版本
        optional<string> nvml_version_;         ///< NVML 版本
        optional<uint32_t> utilization_;        ///< 计算利用率
        optional<uint32_t> memory_utilization_; ///< 显存带宽利用率
        optional<uint64_t> memory_used_;        ///< 已用显存（字节）
        optional<uint64_t> memory_total_;       ///< 显存总量（字节）
        optional<uint32_t> temperature_;        ///< 温度（摄氏度）
        optional<uint32_t> fan_;                ///< 风扇转速百分比
        optional<float> power_watts_;           ///< 功耗（瓦）
        optional<float> power_limit_watts_;     ///< 功耗上限（瓦）
        optional<uint32_t> clock_mhz_;          ///< 核心频率（MHz）
        optional<uint32_t> memory_clock_mhz_;   ///< 显存频率（MHz）
        optional<uint32_t> encoder_;            ///< 编码利用率
        optional<uint32_t> decoder_;            ///< 解码利用率
        optional<uint32_t> pcie_rx_kib_;        ///< PCIe 接收（KiB/s）
        optional<uint32_t> pcie_tx_kib_;        ///< PCIe 发送（KiB/s）
        optional<string> performance_state_;    ///< 性能状态
        vector<gpu_process> processes_;         ///< 关联进程
        string process_status_;                 ///< 进程采样状态
        bool unified_memory_ = false;           ///< 是否与系统内存共享
        optional<uint64_t> memory_limit_;       ///< GPU 可用内存上限（字节）
        json extra_;                            ///< 扩展字段

        /**
         * @brief 获取显存标签
         * @return 独立显存为 `VRAM`，共享内存为 `UMEM`
         */
        NEFORCE_NODISCARD string memory_label() const;

        /**
         * @brief 获取显存容量基准
         * @return 容量（字节），未知时为空
         */
        NEFORCE_NODISCARD optional<uint64_t> memory_capacity() const;

        /**
         * @brief 获取显存占用百分比
         * @return 百分比，未知时为空
         */
        NEFORCE_NODISCARD optional<double> memory_percent() const;
    };

    /**
     * @brief 网络连接采样
     */
    struct connection {
        vector<tag> tags_;                 ///< 工作负载标签
        string id_;                        ///< 连接标识
        string protocol_;                  ///< 协议
        string local_;                     ///< 本地端点
        string remote_;                    ///< 远端端点
        string state_;                     ///< 连接状态
        uint64_t inode_ = 0;               ///< socket inode
        vector<uint32_t> pids_;            ///< 所属进程
        vector<string> process_names_;     ///< 进程名
        optional<uint64_t> receive_queue_; ///< 接收队列
        optional<uint64_t> send_queue_;    ///< 发送队列
        optional<uint64_t> received_;      ///< 累计接收字节
        optional<uint64_t> sent_;          ///< 累计发送字节
        optional<double> rx_per_sec_;      ///< 接收速率
        optional<double> tx_per_sec_;      ///< 发送速率
        optional<double> rtt_ms_;          ///< 往返时延（毫秒）
        optional<uint64_t> retransmits_;   ///< 重传次数
    };

    /**
     * @brief 连接采样批次
     */
    struct connection_sample {
        uint64_t sampled_at_ = 0;           ///< 采样时间
        uint64_t owners_sampled_at_ = 0;    ///< 归属扫描时间
        string status_;                     ///< 采样状态
        vector<string> notes_;              ///< 提示
        bool owner_scan_complete_ = false;  ///< 归属扫描是否完整
        uint64_t unreadable_processes_ = 0; ///< 不可读进程数
        bool truncated_ = false;            ///< 是否被截断
        vector<connection> connections_;    ///< 连接列表
    };

    /**
     * @brief 容器采样
     */
    struct container {
        vector<tag> tags_;                     ///< 工作负载标签
        string id_;                            ///< 容器标识
        string runtime_;                       ///< 容器运行时
        string name_;                          ///< 容器名
        string image_;                         ///< 镜像
        string state_;                         ///< 状态
        string status_;                        ///< 状态详情
        optional<double> cpu_;                 ///< CPU 百分比
        optional<uint64_t> memory_;            ///< 内存占用
        optional<uint64_t> memory_limit_;      ///< 内存上限
        optional<uint64_t> network_rx_;        ///< 累计接收
        optional<uint64_t> network_tx_;        ///< 累计发送
        optional<uint64_t> pids_;              ///< 进程数
        string ports_;                         ///< 端口映射
        string command_;                       ///< 启动命令
        string created_;                       ///< 创建时间
        string running_for_;                   ///< 运行时长
        optional<double> memory_percent_;      ///< 内存百分比
        optional<uint64_t> block_read_;        ///< 累计块读取
        optional<uint64_t> block_write_;       ///< 累计块写入
        optional<double> network_rx_per_sec_;  ///< 网络接收速率
        optional<double> network_tx_per_sec_;  ///< 网络发送速率
        optional<double> block_read_per_sec_;  ///< 块读取速率
        optional<double> block_write_per_sec_; ///< 块写入速率
    };

    /**
     * @brief 服务采样
     */
    struct service {
        vector<tag> tags_;               ///< 工作负载标签
        string name_;                    ///< 服务名
        string scope_;                   ///< 作用域
        string load_;                    ///< 单元加载状态
        string active_;                  ///< 活动状态
        string sub_;                     ///< 子状态
        string description_;             ///< 描述
        optional<uint32_t> pid_;         ///< 主进程
        optional<float> cpu_percent_;    ///< 主进程 CPU
        optional<uint64_t> memory_;      ///< 内存占用
        optional<uint64_t> cpu_time_ns_; ///< 累计 CPU 时间
        optional<uint64_t> tasks_;       ///< 任务数
        json extra_;                     ///< 扩展字段
    };

    /**
     * @brief 虚拟机采样
     */
    struct virtual_machine {
        vector<tag> tags_;              ///< 工作负载标签
        string name_;                   ///< 域名或 QEMU 进程名
        string id_;                     ///< libvirt ID 或进程号
        string uuid_;                   ///< 虚拟机 UUID
        string backend_;                ///< libvirt / QEMU
        string state_;                  ///< running / paused / shut off
        string autostart_;              ///< 自动启动状态
        string description_;            ///< 描述或命令行摘要
        optional<uint32_t> pid_;        ///< QEMU 进程号
        optional<uint32_t> vcpus_;      ///< 虚拟 CPU 数
        optional<uint64_t> memory_;     ///< 当前内存（字节）
        optional<uint64_t> max_memory_; ///< 最大内存（字节）
        optional<double> cpu_percent_;  ///< 宿主机 CPU 占用
        json extra_;                    ///< 扩展字段
    };

    /**
     * @brief 监听端口采样
     */
    struct listener {
        string protocol_;   ///< 协议
        string address_;    ///< 地址
        uint16_t port_ = 0; ///< 端口
        uint32_t uid_ = 0;  ///< 用户标识
    };

    /**
     * @brief 本机发现清单
     */
    struct inventory {
        uint64_t updated_at_ = 0;                  ///< 更新时间
        vector<container> containers_;             ///< 容器
        vector<string> runtimes_;                  ///< 发现的运行时
        vector<service> services_;                 ///< 服务
        vector<string> service_managers_;          ///< 服务管理器
        vector<virtual_machine> virtual_machines_; ///< 虚拟机
        vector<string> vm_managers_;               ///< 虚拟机管理器
        vector<listener> listeners_;               ///< 监听端口
        vector<string> notes_;                     ///< 提示
        json extra_;                               ///< 扩展字段
    };

    /**
     * @brief 一次完整的系统采样
     */
    struct snapshot {
        uint64_t timestamp_ = 0;                    ///< UNIX 秒
        double elapsed_ = 0.0;                      ///< 程序运行时长（秒）
        double collection_ms_ = 0.0;                ///< 本次采集耗时（毫秒）
        string hostname_;                           ///< 主机名
        string os_;                                 ///< 操作系统
        string kernel_;                             ///< 内核版本
        uint64_t uptime_ = 0;                       ///< 开机时长（秒）
        string cpu_name_;                           ///< CPU 型号
        float cpu_percent_ = 0.0F;                  ///< 总 CPU 占用
        uint64_t cpu_mhz_ = 0;                      ///< CPU 平均频率
        json cpu_breakdown_;                        ///< CPU 时间构成
        vector<float> cores_;                       ///< 每逻辑核占用
        uint32_t pcore_count_ = 0;                  ///< 性能核数量
        uint32_t ecore_count_ = 0;                  ///< 能效核数量
        vector<optional<float>> core_temperatures_; ///< 每逻辑核温度
        float load_[3] = {0.0F, 0.0F, 0.0F};        ///< 1/5/15 分钟负载
        memory memory_;                             ///< 内存
        vector<process> processes_;                 ///< 进程
        vector<network> networks_;                  ///< 网络接口
        connection_sample connections_;             ///< 连接
        vector<disk> disks_;                        ///< 文件系统
        vector<disk_io> disk_io_;                   ///< 块设备 I/O
        vector<sensor> sensors_;                    ///< 温度传感器
        vector<battery> batteries_;                 ///< 电池
        vector<gpu> gpus_;                          ///< GPU
        string gpu_status_;                         ///< GPU 采集状态
        vector<pressure> pressure_;                 ///< 系统压力
        vector<string> alerts_;                     ///< 告警
        inventory inventory_;                       ///< 发现清单
        json extra_;                                ///< 扩展字段

        /**
         * @brief 合并发现清单
         * @param data 发现清单
         */
        void apply_inventory(inventory data);
    };

    /**
     * @brief 计算百分比
     * @param used 已用量
     * @param total 总量
     * @return 0 到 100 的百分比
     */
    NEFORCE_NODISCARD double percent(uint64_t used, uint64_t total);

    /**
     * @brief 格式化字节数
     * @param value 字节数
     * @return 二进制单位文本
     */
    NEFORCE_NODISCARD string bytes(float64_t value);

    /**
     * @brief 格式化时长
     * @param seconds 秒
     * @return `HH:MM:SS` 或 `Nd HHh`
     */
    NEFORCE_NODISCARD string duration(uint64_t seconds);

    /**
     * @brief 判断接口是否为环回接口
     * @param name 接口名
     * @return 环回返回 true
     */
    NEFORCE_NODISCARD bool is_loopback(const string& name);

    /**
     * @brief 获取 GPU 历史键
     * @param device GPU
     * @return 稳定键
     */
    NEFORCE_NODISCARD string gpu_key(const gpu& device);

    /**
     * @brief 获取容器历史键
     * @param item 容器
     * @return 稳定键
     */
    NEFORCE_NODISCARD string container_key(const container& item);

    /**
     * @brief 序列化快照
     * @param value 快照
     * @return JSON 值
     */
    NEFORCE_NODISCARD json to_json(const snapshot& value);

    /**
     * @brief 反序列化快照
     * @param value JSON 值
     * @return 快照
     */
    NEFORCE_NODISCARD snapshot snapshot_from_json(const json& value);

    /**
     * @brief 序列化连接采样
     * @param value 连接采样
     * @return JSON 值
     */
    NEFORCE_NODISCARD json to_json(const connection_sample& value);

    /**
     * @brief 序列化发现清单
     * @param value 发现清单
     * @return JSON 值
     */
    NEFORCE_NODISCARD json to_json(const inventory& value);

    /**
     * @brief 反序列化发现清单
     * @param value JSON 值
     * @return 发现清单
     */
    NEFORCE_NODISCARD inventory inventory_from_json(const json& value);

    /**
     * @brief 序列化标签
     * @param value 标签
     * @return JSON 值
     */
    NEFORCE_NODISCARD json to_json_value(const tag& value);

    /**
     * @brief 反序列化标签
     * @param value JSON 值
     * @param out 标签
     */
    void from_json_value(const json& value, tag& out);

} // namespace neforce::neflux
#endif // NEXUSFLUX_MODEL_HPP__

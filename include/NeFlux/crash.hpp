#ifndef NEFLUX_CRASH_HPP__
#define NEFLUX_CRASH_HPP__
namespace neforce::neflux {

    /**
     * @brief 初始化进程级日志与崩溃捕获。
     *
     * 日志写入 `<config>/neflux/logs/nexus-flux.log`，启用日期/大小轮转。
     * Windows 注册未处理异常过滤器并尽量生成 minidump；Linux 注册致命信号处理。
     * 初始化失败不会阻止程序启动。
     *
     * @param argc 参数个数
     * @param argv 参数数组
     */
    void initialize_crash_logging(int argc, char** argv);

} // namespace neforce::neflux
#endif // NEFLUX_CRASH_HPP__

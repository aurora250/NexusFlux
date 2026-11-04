#include <NeFlux/ros.hpp>
#include <NeForce/core/exception/exception.hpp>
namespace neforce::neflux {
    optional<ros_session> open_ros_session(const process& value, string& error) {
        static_cast<void>(value);
        error = "ROS discovery is not supported on this platform";
        return none;
    }
    string run_ros_query(const ros_session& session, ros_query type, const string& argument) {
        static_cast<void>(session);
        static_cast<void>(type);
        static_cast<void>(argument);
        NEFORCE_THROW_EXCEPTION(value_exception("ROS discovery is not supported on this platform"));
    }
    string ros_summary(const ros_session& session) {
        static_cast<void>(session);
        return "ROS discovery is not supported on this platform";
    }
} // namespace neforce::neflux

#include <getopt.h>
#include <linux/bt-bmc.h>
#include <CLI/CLI.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

using namespace phosphor::logging;

namespace
{

namespace io_control
{

struct SetSmsAttention
{
    // Get the name of the IO control command.
    int name() const
    {
        return static_cast<int>(BT_BMC_IOCTL_SMS_ATN);
    }
    // Get the address of the command data.
    boost::asio::detail::ioctl_arg_type* data()
    {
        return nullptr;
    }
};
} // namesapce io_control

class btbridge
{

  public:
    static constexpr size_t btMessageSize = 64;
    static constexpr uint8_t netFnShift = 2;
    static constexpr uint8_t lunMask = (1 << netFnShift) - 1;
    static constexpr const char bt_bmc_device[] = "/dev/ipmi-bt-host";


    btbridge(std::shared_ptr<boost::asio::io_context>& io,
               std::shared_ptr<sdbusplus::asio::connection>& bus,
               const std::string& device, bool verbose) :
        io(io),
        bus(bus), verbose(verbose)
    {
       
        int fd = open(bt_bmc_device, O_RDWR | O_NONBLOCK);
	if (fd < 0) {

            log<level::ERR>("Couldn't open BT device O_RDWR",
                            entry("FILENAME=%s", bt_bmc_device),
                            entry("ERROR=%s", strerror(errno)));
            return;
	}
        else
        {
            dev = std::make_unique<boost::asio::posix::stream_descriptor>(*io,
                                                                          fd);
        }
        

        server = std::make_shared<sdbusplus::asio::object_server>(bus);
        static constexpr const char path[] = "/xyz/openbmc_project/Ipmi/Channel/bt1";
        std::shared_ptr<sdbusplus::asio::dbus_interface> iface =
            server->add_interface(path, "xyz.openbmc_project.Ipmi.Channel.bt1");

        iface->register_method("setAttention",
                               [this]() { return setAttention(); });


        iface->initialize();
        static constexpr const char busBase[] = "xyz.openbmc_project.Ipmi.Channel.bt1";
        std::string busName(busBase);
        bus->request_name(busName.c_str());


        async_read(); 
    }

    int64_t setAttention()
    {
        if (verbose)
        {
            log<level::INFO>("Sending SET_SMS_ATTENTION");
        }
        io_control::SetSmsAttention command;
        boost::system::error_code ec;
        dev->io_control(command, ec);
        if (ec)
        {
            log<level::ERR>("Couldn't SET_SMS_ATTENTION",
                            entry("ERROR=%s", ec.message().c_str()));
            return ec.value();
        }
        return 0;
    }

    void channelAbort(const char* msg, const boost::system::error_code& ec)
    {
        log<level::ERR>(msg, entry("ERROR=%s", ec.message().c_str()));
        io->stop();
    }

    void async_read()
    {
        sd_journal_print(LOG_INFO,"calling async_read()");
        boost::asio::async_read(
            *dev, boost::asio::buffer(xferBuffer, xferBuffer.size()),
            boost::asio::transfer_at_least(4),
            [this](const boost::system::error_code& ec, size_t rlen) {
                processMessage(ec, rlen);
            });
    }

    void processMessage(const boost::system::error_code& ecRd, size_t rlen)
    {
        sd_journal_print(LOG_INFO, "rlen =  0x%02x",rlen);

        if (ecRd || rlen < 4)
        {
            channelAbort("Failed to read req msg", ecRd);
            return;
        }

        auto rawIter = xferBuffer.cbegin();
        auto rawEnd = rawIter + rlen;
        uint8_t len = *rawIter++;
        sd_journal_print(LOG_INFO, "0x%02x", rlen);
        uint8_t netfn = *rawIter >> netFnShift;
        uint8_t lun = *rawIter++ & lunMask;
        uint8_t seq = *rawIter++;
        uint8_t cmd = *rawIter++;

        if (verbose)
        {
            log<level::INFO>("Read req msg", entry("NETFN=0x%02x", netfn),
                             entry("LUN=0x%02x", lun),
                             entry("SEQ=0x%02x", seq),
                             entry("CMD=0x%02x", cmd));
        }

        std::vector<uint8_t> data(rawIter, rawEnd);
        std::map<std::string, sdbusplus::message::variant<int>> options;

        using IpmiDbusRspType = std::tuple<uint8_t, uint8_t, uint8_t, uint8_t,
                                           std::vector<uint8_t>>;
        static constexpr const char ipmiQueueService[] =
            "xyz.openbmc_project.Ipmi.Host";
        static constexpr const char ipmiQueuePath[] =
            "/xyz/openbmc_project/Ipmi";
        static constexpr const char ipmiQueueIntf[] =
            "xyz.openbmc_project.Ipmi.Server";
        static constexpr const char ipmiQueueMethod[] = "execute";


        bus->async_method_call(
            [this, netfnCap{netfn}, lunCap{lun},seqId{seq},
             cmdCap{cmd}](const boost::system::error_code& ec,
                          const IpmiDbusRspType& response) {
                std::vector<uint8_t> rsp;
                const auto& [netfn, lun, cmd, cc, payload] = response;


                if (ec)
                {
                    log<level::ERR>(
                        "bt<->ipmid bus error:", entry("NETFN=0x%02x", netfn),
                        entry("LUN=0x%02x", lun), entry("CMD=0x%02x", cmd),
                        entry("ERROR=%s", ec.message().c_str()));
                    
                    constexpr uint8_t ccResponseNotAvailable = 0xce;
                    rsp.resize(sizeof(netfn) + sizeof(cmd) + sizeof(cc));
                    rsp[0] =
                        ((netfnCap + 1) << netFnShift) | (lunCap & lunMask);
                    rsp[1] = seqId;
                    rsp[2] = cmdCap;
                    rsp[3] = ccResponseNotAvailable;
                }
                else
                {

                    uint8_t len = sizeof(netfn) + sizeof(cmd) + sizeof(seqId) + sizeof(cc) + payload.size();
                    rsp.resize(sizeof(len) + sizeof(netfn) + sizeof(cmd) + sizeof(seqId) + sizeof(cc) +
                               payload.size());
                    sd_journal_print(LOG_INFO, "rsp len =  0x%02x payload size= 0x%02x",rsp.size(), payload.size());
           
                    auto rspIter = rsp.begin();
                    *rspIter++ = len;
                    *rspIter++ = (netfn << netFnShift) | (lun & lunMask);
                    *rspIter++ = seqId;
                    *rspIter++ = cmd;
                    *rspIter++ = cc;
                    if (payload.size())
                    {
                        std::copy(payload.cbegin(), payload.cend(), rspIter);
                    }
                }
                if (verbose)
                {
                    log<level::INFO>(
                        "Send rsp msg", entry("NETFN=0x%02x", netfn),
                        entry("LUN=0x%02x", lun), entry("CMD=0x%02x", cmd),
                        entry("SEQ=0x%02x", seqId),
                        entry("CC=0x%02x", cc));

                }

                boost::system::error_code ecWr;
                size_t wlen =
                    boost::asio::write(*dev, boost::asio::buffer(rsp), ecWr);
                if (ecWr || wlen != rsp.size())
                {
                    log<level::ERR>(
                        "Failed to send rsp msg", entry("SIZE=%d", wlen),
                        entry("EXPECT=%d", rsp.size()),
                        entry("ERROR=%s", ecWr.message().c_str()),
                        entry("NETFN=0x%02x", netfn), entry("LUN=0x%02x", lun),
                        entry("CMD=0x%02x", cmd), entry("CC=0x%02x", cc));
                }
                else
                {
                    async_read();
                }
            },
            ipmiQueueService, ipmiQueuePath, ipmiQueueIntf, ipmiQueueMethod,
            netfn, lun, cmd, data, options);
    }

  protected:
    std::array<uint8_t, btMessageSize> xferBuffer;
    std::shared_ptr<boost::asio::io_context> io;
    std::shared_ptr<sdbusplus::asio::connection> bus;
    std::shared_ptr<sdbusplus::asio::object_server> server;
    std::unique_ptr<boost::asio::posix::stream_descriptor> dev = nullptr;
    bool verbose;
};


}

int main(int argc, char* argv[])
{
    CLI::App app("BT IPMI bridge");
    std::string device;
    app.add_option("-d,--device", device, "device name");

    bool verbose = true;
    app.add_option("-v,--verbose", verbose, "print more verbose output");

    CLI11_PARSE(app, argc, argv);

    auto io = std::make_shared<boost::asio::io_context>();
    sd_bus* dbus;
    sd_bus_default_system(&dbus);
    auto bus = std::make_shared<sdbusplus::asio::connection>(*io, dbus);

    btbridge bt(io, bus, device, verbose);


    io->run();

    return 0;
}


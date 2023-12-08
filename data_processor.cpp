#include <iostream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <boost/asio.hpp>
#include "json.hpp"
#include "mqtt/client.h"
#include "mqtt/connect_options.h"

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"
#define GRAPHITE_HOST "127.0.0.1"
#define GRAPHITE_PORT 2003

namespace asio = boost::asio;
using asio::ip::tcp;

void publish_to_graphite(const std::string &metric) {
    try {
        asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);
        asio::connect(socket, resolver.resolve(GRAPHITE_HOST, std::to_string(GRAPHITE_PORT)));

        // Send metric to Graphite server
        asio::write(socket, asio::buffer(metric));
    } catch (const std::exception &e) {
        std::cerr << "Error in publish_to_graphite: " << e.what() << std::endl;
    }
}

std::string timestamp2UNIX(const std::string &timestamp)
{
    std::tm t = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");
    std::time_t time_stamp = mktime(&t);
    return std::to_string(time_stamp);
}

void post_metric(const std::string &machine_id, const std::string &sensor_id, const std::string &timestamp_str, const int value)
{
    try
    {
        // Create metric path using machine-id and sensor-id
        std::string metric_path = machine_id + "." + sensor_id;

        // Prepare the metric value and timestamp
        std::ostringstream metric_value_stream;
        metric_value_stream << value;
        std::string metric_value = metric_value_stream.str();

        std::ostringstream metric_timestamp_stream;
        metric_timestamp_stream << timestamp_str;
        std::string metric_timestamp = metric_timestamp_stream.str();

        // Construct the Graphite metric string
        std::string graphite_metric = metric_path + " " + std::to_string(value) + " " + timestamp2UNIX(timestamp_str) + "\n";

        // Publish the metric to Graphite
        publish_to_graphite(graphite_metric);

        // For demonstration, let's print the metric string
        std::string print_graphite_metric = metric_path + " " + std::to_string(value) + " " + timestamp_str;
        std::cout << "posted: " << graphite_metric << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error in post_metric: " << e.what() << std::endl;
    }
}

// TO DO: criar função para processamento de dados no Mosquitto
// void processDataMosquitto() {}

std::vector<std::string> split(const std::string &str, char delim)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delim))
    {
        tokens.push_back(token);
    }
    return tokens;
}

int main(int argc, char *argv[])
{
    std::string clientId = "clientId";
    mqtt::async_client client(BROKER_ADDRESS, clientId);

    // Create an MQTT callback.
    class callback : public virtual mqtt::callback
    {
    public:
        void message_arrived(mqtt::const_message_ptr msg) override
        {
            auto j = nlohmann::json::parse(msg->get_payload());

            std::string topic = msg->get_topic();
            auto topic_parts = split(topic, '/');
            std::string machine_id = topic_parts[2];
            std::string sensor_id = topic_parts[3];

            std::string timestamp = j["timestamp"];
            int value = j["value"];
            post_metric(machine_id, sensor_id, timestamp, value);
        }
        /*
            TO DO: 
            - Monitorar a atividade de sensores na máquina, 
            - Registrar o tempo da última atividade para cada par máquina-sensor
            - Se um valor de sensor for outlier, gera e envia um alarme para o Graphite        
        */
    };

    callback cb;
    client.set_callback(cb);

    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try
    {
        client.connect(connOpts)->wait();
        client.subscribe("/sensors/#", QOS);
        std::cout << "Subscrided\n";
    }
    catch (mqtt::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    while (true)
    {
        // TO DO: chamar aqui a função de processamento de dados
        // processDataMosquitto()
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return EXIT_SUCCESS;
}

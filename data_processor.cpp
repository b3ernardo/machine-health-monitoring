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
using namespace std;
using asio::ip::tcp;

// Função auxiliar para publicar a métrica no Graphite
void publish_to_graphite(const string &metric)
{
    try
    {
        asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);
        asio::connect(socket, resolver.resolve(GRAPHITE_HOST, to_string(GRAPHITE_PORT)));
        // Envia a métrica ao servidor do Graphite
        asio::write(socket, asio::buffer(metric));
    }
    catch (const exception &e)
    {
        cerr << "error in publish_to_graphite: " << e.what() << endl;
    }
}

// Função para converter timestamp em Unix
string timestamp_to_unix(const string &timestamp)
{
    tm t = {};
    istringstream ss(timestamp);
    ss >> get_time(&t, "%Y-%m-%dT%H:%M:%S");
    time_t time_stamp = mktime(&t);
    return to_string(time_stamp);
}

// Função para criar e postar a métrica no Graphite
void post_metric(const string &machine_id, const string &sensor_id, const string &timestamp_str, const int value)
{
    try
    {
        // Cria o caminho da métrica usando o machine_id e o sensor_id
        string metric_path = machine_id + "." + sensor_id;
        // Prepara o valor da métrica and timestamp
        ostringstream metric_value_stream;
        metric_value_stream << value;
        string metric_value = metric_value_stream.str();
        // Prepara o timestamp da métrica
        ostringstream metric_timestamp_stream;
        metric_timestamp_stream << timestamp_str;
        string metric_timestamp = metric_timestamp_stream.str();
        // Constrói a métrica em uma string
        string graphite_metric = metric_path + " " + to_string(value) + " " + timestamp_to_unix(timestamp_str) + "\n";
        // Publica a métrica no Graphite
        publish_to_graphite(graphite_metric);
        // Printa a métrica postada no Graphite
        string print_graphite_metric = metric_path + " " + to_string(value) + " " + timestamp_str;
        cout << "posted: " << print_graphite_metric << endl << endl;
    }
    catch (const exception &e)
    {
        cerr << "error in post_metric: " << e.what() << endl;
    }
}

vector<string> split(const string &str, char delim)
{
    vector<string> tokens;
    string token;
    istringstream token_stream(str);
    while (getline(token_stream, token, delim))
    {
        tokens.push_back(token);
    }
    return tokens;
}

int main(int argc, char *argv[])
{
    string client_id = "client_id";
    mqtt::async_client client(BROKER_ADDRESS, client_id);
    // Cria um callback MQTT
    class callback : public virtual mqtt::callback
    {
    public:
        void message_arrived(mqtt::const_message_ptr msg) override
        {
            auto j = nlohmann::json::parse(msg->get_payload());

            string topic = msg->get_topic();
            auto topic_parts = split(topic, '/');
            string machine_id = topic_parts[2];
            string sensor_id = topic_parts[3];

            string timestamp = j["timestamp"];
            int value = j["value"];
            post_metric(machine_id, sensor_id, timestamp, value);
        }
    };
    callback cb;
    client.set_callback(cb);
    // Conecta ao broker MQTT
    mqtt::connect_options conn_opts;
    conn_opts.set_keep_alive_interval(20);
    conn_opts.set_clean_session(true);
    try
    {
        client.connect(conn_opts)->wait();
        client.subscribe("/sensors/#", QOS);
        cout << "subscribed" << endl << endl;
    }
    catch (mqtt::exception &e)
    {
        cerr << "error: " << e.what() << endl;
        return EXIT_FAILURE;
    }
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(1));
    }
    return EXIT_SUCCESS;
}
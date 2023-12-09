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

// Função para converter timestamp em Unix
string timestamp_to_unix(const string &timestamp)
{
    tm t = {};
    istringstream ss(timestamp);
    ss >> get_time(&t, "%Y-%m-%dT%H:%M:%S");
    time_t time_stamp = mktime(&t);
    return to_string(time_stamp);
}

// Função para converter Unix em timestamp
string unix_to_timestamp(const time_t &timestamp)
{
    tm *ptm = localtime(&timestamp);
    char buffer[32];
    strftime(buffer, 32, "%Y-%m-%dT%H:%M:%S", ptm);
    return string(buffer);
}

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

// Função para criar e postar a métrica no Graphite
void post_metric(const string &machine_id, const string &sensor_id, const string &timestamp_str, const int value)
{
    try
    {
        // Cria o caminho da métrica usando o machine_id e o sensor_id
        string metric_path = machine_id + "." + sensor_id;
        // Constrói a métrica em uma string
        string graphite_metric = metric_path + " " + to_string(value) + " " + timestamp_to_unix(timestamp_str) + "\n";
        // Publica a métrica no Graphite
        publish_to_graphite(graphite_metric);
        // Printa a métrica postada no Graphite
        string print_graphite_metric = metric_path + " " + to_string(value) + " " + timestamp_str;
        cout << "posted: " << print_graphite_metric << endl
             << endl;
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

// Map para rastrear o último tempo de atividade de cada sensor
map<pair<string, string>, chrono::steady_clock::time_point> last_sensor_activity;
// Função para atualizar o tempo da última atividade para o tempo atual
void update_sensor_activity(const string &machine_id, const string &sensor_id)
{
    last_sensor_activity[{machine_id, sensor_id}] = chrono::steady_clock::now();
}

// Obtém o tempo atual
chrono::steady_clock::time_point start_time = chrono::steady_clock::now();
// Obtém a frequência do sensor
double find_sensor_frequency()
{
    chrono::steady_clock::time_point end_time = chrono::steady_clock::now();
    chrono::duration<double> elapsed_time = end_time - start_time;
    start_time = end_time;
    return elapsed_time.count() * 10;
}

void process_data()
{
    time_t current_time = time(nullptr);
    double frequency = find_sensor_frequency();
    // Iteração sobre todas atividades do sensor
    for (auto &activity : last_sensor_activity)
    {
        auto machine_sensor_pair = activity.first;
        auto last_activity_time = activity.second;
        auto machine_id = machine_sensor_pair.first;
        auto sensor_id = machine_sensor_pair.second;
        // Calcula o tempo decorrido desde a última atividade
        auto elapsed_time = chrono::steady_clock::now() - last_activity_time;
        auto elapsed_time_in_seconds = chrono::duration_cast<chrono::seconds>(elapsed_time).count();
        // Verifica se o tempo decorrido é maior ou igual a 10 períodos
        if (elapsed_time_in_seconds >= frequency)
        {
            // Gera o alarme de inatividade
            string alarm_path = machine_id + ".alarms.inactive." + sensor_id;
            string message = alarm_path + " 1 " + unix_to_timestamp(current_time) + "\n";
            post_metric(machine_id, "alarms.inactive." + sensor_id, unix_to_timestamp(current_time), 1);
        }
    }
}

class MQTTCallback : public virtual mqtt::callback
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
        // Atualiza o tempo da última atividade ao receber um novo dado do sensor
        update_sensor_activity(machine_id, sensor_id);
    }
};

int main(int argc, char *argv[])
{
    string client_id = "client_id";
    mqtt::async_client client(BROKER_ADDRESS, client_id);
    MQTTCallback cb;
    client.set_callback(cb);
    mqtt::connect_options conn_opts;
    conn_opts.set_keep_alive_interval(20);
    conn_opts.set_clean_session(true);
    try
    {
        client.connect(conn_opts)->wait();
        client.subscribe("/sensors/#", QOS);
        cout << "subscribed" << endl
             << endl;
    }
    catch (mqtt::exception &e)
    {
        cerr << "error: " << e.what() << endl;
        return EXIT_FAILURE;
    }
    while (true)
    {
        process_data();
        this_thread::sleep_for(chrono::seconds(1));
    }
    return EXIT_SUCCESS;
}
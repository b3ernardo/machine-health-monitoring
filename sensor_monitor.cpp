#include <iostream>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include "json.hpp"
#include "mqtt/client.h"
#include <iomanip>
#include <sstream>
#include <vector>
#include <sys/sysinfo.h>

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"
#define MONITOR_TOPIC "/sensor_monitors"

using namespace std;

// Estrutura para armazenar informações do sensor
struct Sensor
{
    string sensor_id;
    string data_type;
    long data_interval;
};

// Função para obter a RAM usada, em MB
int get_used_RAM()
{
    struct sysinfo info;
    if (sysinfo(&info) != 0)
    {
        cerr << "error getting system information" << endl;
        return -1;
    }
    // Calcula a RAM usada (RAM total - RAM livre)
    int used_ram_in_mb = (info.totalram - info.freeram) / (1024 * 1024);
    return used_ram_in_mb;
}

// Função para obter o número de processos em execução
int get_running_processes()
{
    struct sysinfo info;
    if (sysinfo(&info) != 0)
    {
        cerr << "error getting system information" << endl;
        return -1;
    }
    return info.procs;
}

// Função para publicar a mensagem inicial
void publish_initial_message(const string &machine_id, const vector<Sensor> &sensors, mqtt::client &client)
{
    // Cria um objeto JSON para a mensagem inicial
    nlohmann::json initial_message;
    initial_message["machine_id"] = machine_id;
    vector<nlohmann::json> sensors_json;
    // Para cada sensor, cria um objeto JSON com suas informações e adiciona ao vetor
    for (const auto &sensor : sensors)
    {
        nlohmann::json sensor_json;
        sensor_json["sensor_id"] = sensor.sensor_id;
        sensor_json["data_type"] = sensor.data_type;
        sensor_json["data_interval"] = sensor.data_interval;
        sensors_json.push_back(sensor_json);
    }
    // Adiciona o vetor de sensores à mensagem JSON
    initial_message["sensors"] = sensors_json;
    // Cria uma mensagem MQTT com a mensagem JSON e publica para o tópico de monitoramento
    mqtt::message msg(MONITOR_TOPIC, initial_message.dump(), QOS, false);
    client.publish(msg);
    // Exibe informações no console
    clog << "initial message published - topic: " << MONITOR_TOPIC << " - message: " << initial_message.dump() << endl
         << endl;
}

// Função para publicar mensagens periodicamente
void publish_initial_message_periodically(const string &machine_id, const vector<Sensor> &sensors, mqtt::client &client, int interval)
{
    while (true)
    {
        // Publica a mensagem inicial
        publish_initial_message(machine_id, sensors, client);
        // Dorme pelo intervalo especificado
        this_thread::sleep_for(chrono::seconds(interval));
    }
}

// Função para ler e publicar dados do sensor
void read_and_publish_sensor(const string &machine_id, const Sensor &sensor, mqtt::client &client)
{
    // Monta o tópico MQTT para o sensor específico
    string topic = "/sensors/" + machine_id + "/" + sensor.sensor_id;

    while (true)
    {
        int value;
        // Escolhe a função apropriada com base no tipo de sensor
        if (sensor.sensor_id == "used_memory")
        {
            value = get_used_RAM();
        }
        else if (sensor.sensor_id == "running_processes")
        {
            value = get_running_processes();
        }
        else
        {
            cerr << "unknown sensor: " << sensor.sensor_id << endl;
            continue;
        }
        // Obtém o timestamp atual no formato ISO 8601
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        tm *now_tm = localtime(&now_c);
        stringstream ss;
        ss << put_time(now_tm, "%FT%TZ");
        string timestamp = ss.str();
        // Cria um objeto JSON com os dados do sensor
        nlohmann::json sensor_data;
        sensor_data["timestamp"] = timestamp;
        sensor_data["value"] = value;
        // Cria e publica a mensagem MQTT para o tópico do sensor
        mqtt::message msg(topic, sensor_data.dump(), QOS, false);
        client.publish(msg);
        // Exibe informações no console
        clog << "message published - topic: " << topic << " - message: " << sensor_data.dump() << endl
             << endl;
        // Aguarda pelo intervalo definido para a leitura do sensor
        this_thread::sleep_for(chrono::milliseconds(sensor.data_interval));
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cerr << "usage: " << argv[0] << " <initial_msg_interval_in_seconds> <msg_interval_in_milliseconds>" << endl;
        return EXIT_FAILURE;
    }
    string client_id = "sensor-monitor";
    mqtt::client client(BROKER_ADDRESS, client_id);
    // Conecta-se ao broker MQTT
    mqtt::connect_options conn_opts;
    conn_opts.set_keep_alive_interval(20);
    conn_opts.set_clean_session(true);
    try
    {
        client.connect(conn_opts);
    }
    catch (mqtt::exception &e)
    {
        cerr << "error: " << e.what() << endl;
        return EXIT_FAILURE;
    }
    clog << "connected to the broker" << endl
         << endl;
    // Obtém o identificador único da máquina, neste caso, o nome do host
    char hostname[1024];
    gethostname(hostname, 1024);
    string machine_id(hostname);
    // Define sensores e suas propriedades
    vector<Sensor> sensors = {
        {"used_memory", "int", atoi(argv[2])},
        {"running_processes", "int", atoi(argv[2])},
    };
    // Cria uma thread para publicar mensagens periodicamente
    thread publish_thread(publish_initial_message_periodically, ref(machine_id), ref(sensors), ref(client), atoi(argv[1]));
    // Cria threads para cada sensor
    vector<thread> threads;
    for (const auto &sensor : sensors)
    {
        // Adiciona as threads ao vetor de threads
        threads.emplace_back(read_and_publish_sensor, ref(machine_id), sensor, ref(client));
    }
    // Aguarda o término das threads
    for (auto &thread : threads)
    {
        thread.join();
    }
    return EXIT_SUCCESS;
}
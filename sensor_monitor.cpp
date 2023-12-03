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
    long data_interval; // em milissegundos
};

// Função para obter a RAM usada, em MB
int getUsedRAM()
{
    struct sysinfo info;
    if (sysinfo(&info) != 0)
    {
        cerr << "error getting system information" << endl;
        return -1;
    }
    // Calcula a RAM usada (RAM total - RAM livre)
    int usedRAMInMB = (info.totalram - info.freeram) / (1024 * 1024);
    return usedRAMInMB;
}

// Função para obter o número de processos em execução
int getRunningProcesses()
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
void publishInitialMessage(const string &machineId, const vector<Sensor> &sensors, mqtt::client &client)
{
    nlohmann::json initialMessage;
    initialMessage["machine_id"] = machineId;
    vector<nlohmann::json> sensorsJson;
    for (const auto &sensor : sensors)
    {
        nlohmann::json sensorJson;
        sensorJson["sensor_id"] = sensor.sensor_id;
        sensorJson["data_type"] = sensor.data_type;
        sensorJson["data_interval"] = sensor.data_interval;
        sensorsJson.push_back(sensorJson);
    }
    initialMessage["sensors"] = sensorsJson;
    mqtt::message msg(MONITOR_TOPIC, initialMessage.dump(), QOS, false);
    client.publish(msg);
    clog << "initial message published - topic: " << MONITOR_TOPIC << " - message: " << initialMessage.dump() << endl;
}

// Função para publicar mensagens periodicamente
void publishInitialMessagePeriodically(const string &machineId, const vector<Sensor> &sensors, mqtt::client &client, int interval)
{
    while (true)
    {
        // Publica a mensagem inicial
        publishInitialMessage(machineId, sensors, client);
        // Dorme pelo intervalo especificado
        this_thread::sleep_for(chrono::seconds(interval));
    }
}

// Função para ler e publicar dados do sensor
void readAndPublishSensor(const string &machineId, const Sensor &sensor, mqtt::client &client)
{
    string topic = "/sensors/" + machineId + "/" + sensor.sensor_id;
    while (true)
    {
        int value;
        // Escolhe a função apropriada com base no tipo de sensor
        if (sensor.sensor_id == "used_memory")
        {
            value = getUsedRAM();
        }
        else if (sensor.sensor_id == "running_processes")
        {
            value = getRunningProcesses();
        }
        else
        {
            cerr << "unknown sensor: " << sensor.sensor_id << endl;
            continue;
        }
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        tm *now_tm = localtime(&now_c);
        stringstream ss;
        ss << put_time(now_tm, "%FT%TZ");
        string timestamp = ss.str();
        nlohmann::json sensorData;
        sensorData["timestamp"] = timestamp;
        sensorData["value"] = value;
        mqtt::message msg(topic, sensorData.dump(), QOS, false);
        client.publish(msg);
        clog << "message published - topic: " << topic << " - message: " << sensorData.dump() << endl;
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
    string clientId = "sensor-monitor";
    mqtt::client client(BROKER_ADDRESS, clientId);
    // Conecta-se ao broker MQTT.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);
    try
    {
        client.connect(connOpts);
    }
    catch (mqtt::exception &e)
    {
        cerr << "error: " << e.what() << endl;
        return EXIT_FAILURE;
    }
    clog << "connected to the broker" << endl;
    // Obtém o identificador único da máquina, neste caso, o nome do host.
    char hostname[1024];
    gethostname(hostname, 1024);
    string machineId(hostname);
    // Define sensores e suas propriedades
    vector<Sensor> sensors = {
        {"used_memory", "int", atoi(argv[2])},
        {"running_processes", "int", atoi(argv[2])},
    };
    // Cria uma thread para publicar mensagens periodicamente
    thread publishThread(publishInitialMessagePeriodically, ref(machineId), ref(sensors), ref(client), atoi(argv[1]));
    // Cria threads para cada sensor
    vector<thread> threads;
    for (const auto &sensor : sensors)
    {
        threads.emplace_back(readAndPublishSensor, ref(machineId), sensor, ref(client));
    }
    for (auto &thread : threads)
    {
        thread.join();
    }
    return EXIT_SUCCESS;
}
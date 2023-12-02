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

// Estrutura para armazenar informações do sensor
struct Sensor
{
    std::string sensor_id;
    std::string data_type;
    long data_interval; // em milissegundos
};

// Função para obter a RAM usada
long getUsedRAM()
{
    struct sysinfo info;
    if (sysinfo(&info) != 0)
    {
        std::cerr << "Erro ao obter informações do sistema" << std::endl;
        return -1;
    }
    // Calcula a RAM usada (RAM total - RAM livre)
    double usedRAMInMB = static_cast<double>(info.totalram - info.freeram) / (1024 * 1024);
    return usedRAMInMB;
}

// Função para obter o número de processos em execução
int getRunningProcesses()
{
    struct sysinfo info;
    if (sysinfo(&info) != 0)
    {
        std::cerr << "Erro ao obter informações do sistema" << std::endl;
        return -1;
    }
    return info.procs;
}

// Função para publicar a mensagem inicial
void publishInitialMessage(const std::string &machineId, const std::vector<Sensor> &sensors, mqtt::client &client)
{
    nlohmann::json initialMessage;
    initialMessage["machine_id"] = machineId;
    std::vector<nlohmann::json> sensorsJson;
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
    std::clog << "Mensagem inicial publicada - tópico: " << MONITOR_TOPIC << " - mensagem: " << initialMessage.dump() << std::endl;
}

// Função para publicar mensagens periodicamente
void publishMessagesPeriodically(const std::string &machineId, const std::vector<Sensor> &sensors, mqtt::client &client, int interval)
{
    while (true)
    {
        // Publica a mensagem inicial
        publishInitialMessage(machineId, sensors, client);
        // Dorme pelo intervalo especificado
        std::this_thread::sleep_for(std::chrono::seconds(interval));
    }
}

// Função para ler e publicar dados do sensor
void readAndPublishSensor(const std::string &machineId, const Sensor &sensor, mqtt::client &client)
{
    std::string topic = "/sensors/" + machineId + "/" + sensor.sensor_id;
    while (true)
    {
        double value;
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
            std::cerr << "Tipo de sensor desconhecido: " << sensor.sensor_id << std::endl;
            continue;
        }
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm *now_tm = std::localtime(&now_c);
        std::stringstream ss;
        ss << std::put_time(now_tm, "%FT%TZ");
        std::string timestamp = ss.str();
        nlohmann::json sensorData;
        sensorData["timestamp"] = timestamp;
        sensorData["value"] = value;
        mqtt::message msg(topic, sensorData.dump(), QOS, false);
        client.publish(msg);
        std::clog << "Mensagem publicada - tópico: " << topic << " - mensagem: " << sensorData.dump() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(sensor.data_interval));
    }
}

int main(int argc, char *argv[])
{
    std::string clientId = "sensor-monitor";
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
        std::cerr << "Erro: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    std::clog << "Conectado ao broker" << std::endl;
    // Obtém o identificador único da máquina, neste caso, o nome do host.
    char hostname[1024];
    gethostname(hostname, 1024);
    std::string machineId(hostname);
    // Define sensores e suas propriedades
    std::vector<Sensor> sensors = {
        {"used_memory", "float", 1000},
        {"running_processes", "int", 1000},
    };
    // Cria uma thread para publicar mensagens periodicamente
    std::thread publishThread(publishMessagesPeriodically, std::ref(machineId), std::ref(sensors), std::ref(client), 5);
    // Cria threads para cada sensor
    std::vector<std::thread> threads;
    for (const auto &sensor : sensors)
    {
        threads.emplace_back(readAndPublishSensor, std::ref(machineId), sensor, std::ref(client));
    }
    // Aguarda as threads terminarem (isso nunca deve acontecer neste exemplo)
    for (auto &thread : threads)
    {
        thread.join();
    }
    return EXIT_SUCCESS;
}
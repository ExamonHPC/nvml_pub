/*
 * nvml_pub.c : NVIDIA GPU metrics publisher over MQTT
 *
 * (c) 2025 Giacomo Madella, University of Bologna
 *
 * Contributed by:
 * Giacomo Madella <giacomo.madella@unibo.it>
 *
 * Version: v0.1
 *
 * Date:
 * 2025/03/19
 */

 #ifndef _GNU_SOURCE
 #define _GNU_SOURCE
 #endif
 
 #include <stdio.h>
 #include <stdlib.h>
 #include <signal.h>
 #include <unistd.h>
 #include <string.h>
 #include <time.h>
 #include <nvml.h>
 #include "mosquitto.h"
 
 struct mosquitto* mosq;
 int keepRunning;
 const char *version = "v0.1";
 // Structure to hold all GPU metrics
typedef struct {
    unsigned int gpuIndex;
    char deviceName[64];
    unsigned int gpuUtilization;
    unsigned int memoryUtilization;
    unsigned long totalMemory;
    unsigned long usedMemory;
    unsigned long freeMemory;
    unsigned int temperature;
    unsigned int powerUsage;
    nvmlPstates_t performanceState;
    unsigned long bar1Used;
    unsigned long bar1Total;
} GpuMetrics;

void publish_gpu_metrics(nvmlDevice_t device, struct mosquitto *mosq);
void sig_handler(int sig);
void get_timestamp(char *buf);
void usage();
 
// Gather metrics for a single GPU device
int gather_gpu_metrics(nvmlDevice_t device, unsigned int deviceIndex, GpuMetrics *metrics) {
    nvmlUtilization_t utilization;
    nvmlMemory_t memory;
    nvmlReturn_t result;
    nvmlBAR1Memory_t bar1Memory;
    char name[64];
    
    metrics->gpuIndex = deviceIndex;
    
    // Get device name
    if (nvmlDeviceGetName(device, name, sizeof(name)) == NVML_SUCCESS) {
        strncpy(metrics->deviceName, name, sizeof(metrics->deviceName) - 1);
    } else {
        strncpy(metrics->deviceName, "Unknown", sizeof(metrics->deviceName) - 1);
    }
    
    // Get GPU utilization
    result = nvmlDeviceGetUtilizationRates(device, &utilization);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get utilization for GPU %d: %s\n", deviceIndex, nvmlErrorString(result));
        return 0;
    }
    metrics->gpuUtilization = utilization.gpu;
    metrics->memoryUtilization = utilization.memory;
    
    // Get memory info
    result = nvmlDeviceGetMemoryInfo(device, &memory);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get memory info for GPU %d: %s\n", deviceIndex, nvmlErrorString(result));
        return 0;
    }
    metrics->totalMemory = memory.total / (1024 * 1024); // MB
    metrics->usedMemory = memory.used / (1024 * 1024);   // MB
    metrics->freeMemory = memory.free / (1024 * 1024);   // MB
    
    // Get temperature
    result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &metrics->temperature);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get temperature for GPU %d: %s\n", deviceIndex, nvmlErrorString(result));
        return 0;
    }
    
    // Get power usage
    result = nvmlDeviceGetPowerUsage(device, &metrics->powerUsage);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get power usage for GPU %d: %s\n", deviceIndex, nvmlErrorString(result));
        return 0;
    }
    
    // Get performance state
    result = nvmlDeviceGetPerformanceState(device, &metrics->performanceState);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get performance state for GPU %d: %s\n", deviceIndex, nvmlErrorString(result));
        return 0;
    }
    
    // Get BAR1 memory info
    result = nvmlDeviceGetBAR1MemoryInfo(device, &bar1Memory);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get BAR1 memory info for GPU %d: %s\n", deviceIndex, nvmlErrorString(result));
        return 0;
    }
    metrics->bar1Used = bar1Memory.bar1Used / (1024 * 1024); // MB
    metrics->bar1Total = bar1Memory.bar1Total / (1024 * 1024); // MB
    
    return 1;
}

// Publish metrics for all GPU devices
void publish_gpu_metrics(nvmlDevice_t device, struct mosquitto *mosq) {
    unsigned int deviceCount, i;
    nvmlReturn_t result;
    nvmlDevice_t *devices;
    GpuMetrics *allMetrics;
    char topic[128];
    char message[256];
    char timestamp[32];
    
    // Get the number of GPU devices
    result = nvmlDeviceGetCount(&deviceCount);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get device count: %s\n", nvmlErrorString(result));
        return;
    }
    
    // Allocate memory for device handles and metrics
    devices = (nvmlDevice_t*)malloc(deviceCount * sizeof(nvmlDevice_t));
    allMetrics = (GpuMetrics*)malloc(deviceCount * sizeof(GpuMetrics));
    
    if (!devices || !allMetrics) {
        fprintf(stderr, "Failed to allocate memory\n");
        if (devices) free(devices);
        if (allMetrics) free(allMetrics);
        return;
    }
    
    // Gather all metrics from all devices
    for (i = 0; i < deviceCount; i++) {
        result = nvmlDeviceGetHandleByIndex(i, &devices[i]);
        if (result != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get handle for GPU %d: %s\n", i, nvmlErrorString(result));
            continue;
        }
        
        // Gather metrics for this device
        if (!gather_gpu_metrics(devices[i], i, &allMetrics[i])) {
            fprintf(stderr, "Failed to gather metrics for GPU %d\n", i);
        }
    }
    
    // Generate common timestamp for all metrics
    get_timestamp(timestamp);
    
    // Publish all metrics
    for (i = 0; i < deviceCount; i++) {
        // GPU utilization
        snprintf(topic, sizeof(topic), "gpu/%d/utilization", i);
        snprintf(message, sizeof(message), "{ \"value\": %u, \"timestamp\": %s }", 
                allMetrics[i].gpuUtilization, timestamp);
        mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
        
        // Memory utilization
        snprintf(topic, sizeof(topic), "gpu/%d/memory_utilization", i);
        snprintf(message, sizeof(message), "{ \"value\": %u, \"timestamp\": %s }", 
                allMetrics[i].memoryUtilization, timestamp);
        mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
        
        // Memory used
        snprintf(topic, sizeof(topic), "gpu/%d/memory_used", i);
        snprintf(message, sizeof(message), "{ \"value\": %lu, \"total\": %lu, \"timestamp\": %s }", 
                allMetrics[i].usedMemory, allMetrics[i].totalMemory, timestamp);
        mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
        
        // Temperature
        snprintf(topic, sizeof(topic), "gpu/%d/temperature", i);
        snprintf(message, sizeof(message), "{ \"value\": %u, \"timestamp\": %s }", 
                allMetrics[i].temperature, timestamp);
        mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
        
        // Power usage
        snprintf(topic, sizeof(topic), "gpu/%d/power", i);
        snprintf(message, sizeof(message), "{ \"value\": %u, \"timestamp\": %s }", 
                allMetrics[i].powerUsage, timestamp);
        mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
        
        // Performance state
        snprintf(topic, sizeof(topic), "gpu/%d/pstate", i);
        snprintf(message, sizeof(message), "{ \"value\": %d, \"timestamp\": %s }", 
                allMetrics[i].performanceState, timestamp);
        mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
        
        // BAR1 memory
        snprintf(topic, sizeof(topic), "gpu/%d/bar1_memory", i);
        snprintf(message, sizeof(message), "{ \"used\": %lu, \"total\": %lu, \"timestamp\": %s }", 
                allMetrics[i].bar1Used, allMetrics[i].bar1Total, timestamp);
        mosquitto_publish(mosq, NULL, topic, strlen(message), message, 0, false);
    }
    
    // Free allocated memory
    free(devices);
    free(allMetrics);
}
 
 void sig_handler(int sig) {
     keepRunning = 0;
     printf("Clean exit!\n");
 }
 
 void get_timestamp(char *buf) {
     struct timeval tv;
     gettimeofday(&tv, NULL);
     sprintf(buf, "%.3f", tv.tv_sec + (tv.tv_usec / 1000000.0));
 }
 
 void usage() {
     printf("nvml_pub: NVIDIA GPU metrics plugin\n\n");
     printf("Usage: nvml_pub [-h] [-b BROKER] [-p PORT] [-t TOPIC]\n");
     printf("  -h          Show this help message and exit\n");
     printf("  -b BROKER   IP address of the MQTT broker\n");
     printf("  -p PORT     Port of the MQTT broker\n");
     printf("  -t TOPIC    Output topic\n");
     exit(0);
 }
 
 int main(int argc, char* argv[]) {
     int mosqMajor, mosqMinor, mosqRevision;
     char *brokerHost = "localhost";
     int brokerPort = 1883;
     char *topic = "gpu/metrics";
     nvmlReturn_t nvmlResult;
     nvmlDevice_t device;
     char timestamp[64];
 
     // Parse command line arguments
     for (int i = 1; i < argc; i++) {
         if (strcmp(argv[i], "-h") == 0) {
             usage();
         } else if (strcmp(argv[i], "-b") == 0) {
             brokerHost = argv[++i];
         } else if (strcmp(argv[i], "-p") == 0) {
             brokerPort = atoi(argv[++i]);
         } else if (strcmp(argv[i], "-t") == 0) {
             topic = argv[++i];
         }
     }
 
     // Initialize NVML
     nvmlResult = nvmlInit();
     if (nvmlResult != NVML_SUCCESS) {
         fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(nvmlResult));
         return 1;
     }
 
     // Get the first GPU device
     nvmlResult = nvmlDeviceGetHandleByIndex(0, &device);
     if (nvmlResult != NVML_SUCCESS) {
         fprintf(stderr, "Failed to get GPU device handle: %s\n", nvmlErrorString(nvmlResult));
         nvmlShutdown();
         return 1;
     }
 
     // Initialize Mosquitto
     mosquitto_lib_version(&mosqMajor, &mosqMinor, &mosqRevision);
     printf("Initializing Mosquitto Library Version %d.%d.%d\n", mosqMajor, mosqMinor, mosqRevision);
     mosquitto_lib_init();
 
     mosq = mosquitto_new(NULL, true, NULL);
     if (!mosq) {
         perror("Failed to create Mosquitto instance");
         nvmlShutdown();
         return 1;
     }
 
     // Connect to the MQTT broker
     if (mosquitto_connect(mosq, brokerHost, brokerPort, 1000) != MOSQ_ERR_SUCCESS) {
         fprintf(stderr, "Failed to connect to MQTT broker\n");
         mosquitto_destroy(mosq);
         nvmlShutdown();
         return 1;
     }
 
     // Set up signal handlers
     signal(SIGINT, sig_handler);
     signal(SIGTERM, sig_handler);
     keepRunning = 1;
 
     // Main loop
     while (keepRunning) {
         get_timestamp(timestamp);
         printf("[%s] Publishing GPU metrics...\n", timestamp);
 
         publish_gpu_metrics(device, mosq);
 
         sleep(5); // Adjust the sleep duration as needed
     }
 
     // Cleanup
     mosquitto_disconnect(mosq);
     mosquitto_destroy(mosq);
     mosquitto_lib_cleanup();
     nvmlShutdown();
 
     printf("Exiting...\n");
     return 0;
 }
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
 //for true and false:
 #include <stdbool.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <signal.h>
 #include <unistd.h>
 #include <string.h>
 #include <time.h>
 #include <sys/time.h>
 #include <nvml.h>
 #include <mosquitto.h>
 #include <iniparser.h> // Added iniparser include
 
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

// Updated macro to match the simpler format: topic value:timestamp
#define PUB_METRIC(type, name, value, id, format) do { \
    sprintf(topic, "%s/%d/%s", type, id, name); \
    sprintf(data, format ";%s", value, timestamp); \
    if(mosquitto_publish(mosq, NULL, topic, strlen(data), data, 0, false) != MOSQ_ERR_SUCCESS) { \
        if (fp) fprintf(fp, "[MQTT]: Warning: cannot send message.\n"); \
    } \
    if (fp) fprintf(fp, "%s %s\n", topic, data); \
} while(0)


void publish_gpu_metrics(nvmlDevice_t device, struct mosquitto *mosq, char *base_topic);
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
void publish_gpu_metrics(nvmlDevice_t device, struct mosquitto *mosq, char *base_topic) {
    unsigned int deviceCount, i;
    nvmlReturn_t result;
    nvmlDevice_t *devices;
    GpuMetrics *allMetrics;
    char topic[256];
    char data[512];
    char timestamp[32];
    FILE *fp = NULL;
    
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
    
    // Open log file for debug output, similar to pmu_pub
    #ifdef DEBUG
    fp = fopen("nvml_pub.log", "a");
    #endif
    
    // Publish all metrics using the simpler format: value:timestamp
    for (i = 0; i < deviceCount; i++) {
        // GPU utilization
        PUB_METRIC("gpu", "utilization", allMetrics[i].gpuUtilization, i, "%u");
        
        // Memory utilization
        PUB_METRIC("gpu", "memory_utilization", allMetrics[i].memoryUtilization, i, "%u");
        
        // Memory metrics - publish each separately
        PUB_METRIC("gpu", "memory_used", allMetrics[i].usedMemory, i, "%lu");
        PUB_METRIC("gpu", "memory_total", allMetrics[i].totalMemory, i, "%lu");
        PUB_METRIC("gpu", "memory_free", allMetrics[i].freeMemory, i, "%lu");
        
        // Temperature
        PUB_METRIC("gpu", "temperature", allMetrics[i].temperature, i, "%u");
        
        // Power usage (in milliwatts, convert to watts)
        PUB_METRIC("gpu", "power", allMetrics[i].powerUsage / 1000, i, "%u");
        
        // Performance state
        PUB_METRIC("gpu", "pstate", allMetrics[i].performanceState, i, "%d");
        
        // BAR1 memory - publish separately
        PUB_METRIC("gpu", "bar1_used", allMetrics[i].bar1Used, i, "%lu");
        PUB_METRIC("gpu", "bar1_total", allMetrics[i].bar1Total, i, "%lu");
        

    }
    
    // Close log file if opened
    if (fp) fclose(fp);
    
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
     printf("Usage: nvml_pub [-h] [-b BROKER] [-p PORT] [-t TOPIC] [-s INTERVAL]\n");
     printf("  -h                    Show this help message and exit\n");
     printf("  -b BROKER             IP address of the MQTT broker\n");
     printf("  -p PORT               Port of the MQTT broker\n");
     printf("  -t TOPIC              Output topic\n");
     printf("  -s INTERVAL           Sampling interval in seconds\n");
     printf("  -c                    Enable or disable extra metrics\n");
     printf("  -v                    Print version number\n");
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
     float samplingInterval = 5.0;
     int extraMetrics = 1;
     char hostname[256];
     dictionary *ini;
     char tmpstr[256];
     char* conffile = "nvml_pub.conf";
     FILE *fp = stderr;
 
     // Load configuration from file if available
     fprintf(fp, "Using configuration in file: %s\n", conffile);
     ini = iniparser_load(conffile);
     if (ini == NULL) {
         // Try to load from /etc/
         strcpy(tmpstr, "/etc/");
         strcat(tmpstr, conffile);
         ini = iniparser_load(tmpstr);
         if (ini == NULL) {
             fprintf(fp, "Cannot parse file: %s, using defaults\n", conffile);
         }
     }
 
     // If we have a config file, dump it and load values
     if (ini != NULL) {
         fprintf(fp, "\nConf file parameters:\n\n");
         iniparser_dump(ini, stderr);
         
         // Get values from config file
         brokerHost = strdup(iniparser_getstring(ini, "MQTT:brokerHost", brokerHost));
         brokerPort = iniparser_getint(ini, "MQTT:brokerPort", brokerPort);
         topic = strdup(iniparser_getstring(ini, "MQTT:topic", topic));
         samplingInterval = iniparser_getdouble(ini, "Sampling:interval", samplingInterval);
         extraMetrics = iniparser_getboolean(ini, "Sampling:extraMetrics", extraMetrics);
     }
 
     // Parse command line arguments (override config file)
     for (int i = 1; i < argc; i++) {
         if (strcmp(argv[i], "-h") == 0) {
             usage();
         } else if (strcmp(argv[i], "-b") == 0) {
             brokerHost = argv[++i];
         } else if (strcmp(argv[i], "-p") == 0) {
             brokerPort = atoi(argv[++i]);
         } else if (strcmp(argv[i], "-t") == 0) {
             topic = argv[++i];
         } else if (strcmp(argv[i], "-s") == 0) {
             samplingInterval = atof(argv[++i]);
         } else if (strcmp(argv[i], "-c") == 0) {
             extraMetrics = atoi(argv[++i]);
         } else if (strcmp(argv[i], "-v") == 0) {
             printf("Version: %s\n", version);
             exit(0);
         }
     }
 
     // Get hostname for use in topic structure if needed
     if (gethostname(hostname, 255) != 0) {
         fprintf(fp, "Cannot get hostname.\n");
         exit(EXIT_FAILURE);
     }
     hostname[255] = '\0';
     printf("Hostname: %s\n", hostname);
 
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
 
         publish_gpu_metrics(device, mosq, topic);
 
         sleep(samplingInterval); // Using the configured sampling interval
     }
 
     // Cleanup
     mosquitto_disconnect(mosq);
     mosquitto_destroy(mosq);
     mosquitto_lib_cleanup();
     nvmlShutdown();
     
     // Free iniparser dictionary if used
     if (ini != NULL) {
         iniparser_freedict(ini);
     }
 
     printf("Exiting...\n");
     return 0;
 }
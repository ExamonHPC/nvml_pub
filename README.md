# NVIDIA GPU Metrics to MQTT Publisher (NVML)

This script instantiates a service that collects NVIDIA GPU metrics using the NVIDIA Management Library (NVML) and publishes the data to an MQTT broker. The metrics include GPU utilization, memory utilization, temperature, power usage, and memory consumption.

## Prerequisites

This script is intended to be executed on nodes with NVIDIA GPUs and requires access to the management network. It needs the Python MQTT library and the NVIDIA Management Library (NVML).

### Python 

Install the required Python modules:

```bash
pip install -r requirements.txt
```

### NVIDIA Drivers

Ensure that NVIDIA drivers are properly installed on the system. The NVML library is included with the NVIDIA display drivers.

## Configuration

### Config file

Create the configuration file from the example configuration file:

```bash
cp example_nvml_pub.conf nvml_pub.conf
```

Entries of the `.conf` file to be defined for this plugin:

- **MQTT_BROKER**: IP Address of the MQTT broker server.
- **MQTT_PORT**: Port number of the MQTT broker server.
- **MQTT_TOPIC**: The initial section of the MQTT topic which defines the *sensor location* as per Examon datamodel specifications.
- **MQTT_USER**: Username of the MQTT user.
- **MQTT_PASSWORD**: Password of the MQTT user.
- **TS**: Sampling time in seconds.
- **LOG_FILENAME**: Name of the log file.
- **PID_FILENAME**: Name of the PID file.

## Collected Metrics

The NVML publisher collects the following metrics for each GPU:

- **gpu_util**: GPU utilization percentage
- **mem_util**: GPU memory utilization percentage
- **temp**: GPU temperature in Celsius
- **power**: GPU power consumption in Watts
- **mem_used**: GPU memory used in MB

## Options

```
usage: nvml_pub.py [-h] [-b B] [-p P] [-t T] [-s S] [-x X] [-l L] [-L L]
                   [-m M] [-r R] 
                   {run,start,stop,restart}

positional arguments:
  {run,start,stop,restart}
                        Run mode

optional arguments:
  -h, --help            show this help message and exit
  -b B                  IP address of the MQTT broker
  -p P                  Port of the MQTT broker
  -t T                  MQTT topic
  -s S                  Sampling time (seconds)
  -x X                  pid filename
  -l L                  log filename
  -L L                  log level
  -m M                  MQTT username
  -r R                  MQTT password
```

## Run 

Execute as:   

```bash
python nvml_pub.py run
```

## Systemd

This script is intended to be used as a service under systemd. SIGINT should be used as the signal to cleanly stop/kill the running script.

## Example Output

The publisher sends metrics in the following format (i used this format because it is needed in my version of Examon-common, but it should work also with first version since it takes only metric['value'] instead of full json ):

```json
{
  "name": "gpu_util",
  "value": 45,
  "timestamp": 1623456789000,
  "tags": {
    "root": "theta",
    "plugin": "nvml_pub",
    "id": "gpu_0"
  }
}
```

import json
import time
import random
import pynvml  # Need to import pynvml for NVIDIA Management Library

from examon.plugin.examonapp import ExamonApp
from examon.plugin.sensorreader import SensorReader


class Sensor:
    def __init__(self, sensor_name='nvml_pub', range_min=0, range_max=100.0):
        self.sensor_name = sensor_name
        self.range_min = range_min
        self.range_max = range_max
        # Initialize NVML
        try:
            pynvml.nvmlInit()
            self.nvml_initialized = True
            self.device_count = pynvml.nvmlDeviceGetCount()
        except Exception as e:
            print(f"NVML initialization error: {e}")
            self.nvml_initialized = False
            self.device_count = 0
    
    def __del__(self):
        # Clean up NVML on object destruction
        if hasattr(self, 'nvml_initialized') and self.nvml_initialized:
            try:
                pynvml.nvmlShutdown()
            except:
                pass
    
    def get_sensor_data(self):
        payload = []
        
        if not self.nvml_initialized:
            print(f"NVML-inside_get_sensor initialization error: {e}")
            self.nvml_initialized = False
            self.device_count = 0
            return -1

        
        # Read actual GPU data
        timestamp = int(time.time() * 1000)
        mqtt_tpc_dev = self.sensor_name
        
        for device_id in range(self.device_count):
            handle = pynvml.nvmlDeviceGetHandleByIndex(device_id)
            device_name = f"{mqtt_tpc_dev}.gpu{device_id}"
            
            # Performance state
            perfstate = pynvml.nvmlDeviceGetPerformanceState(handle)
            payload.append({
                'sensor_name': f"perf_state",
                'id': str(device_id),
                'value': perfstate,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['perf_state'],
                'values': [perfstate]
            })
            
            # BAR1 memory info
            bar1_info = pynvml.nvmlDeviceGetBAR1MemoryInfo(handle)
            payload.append({
                'sensor_name': f"bar1_Total",
                'id': str(device_id),
                'value': bar1_info.bar1Total,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['bar1_Total'],
                'values': [bar1_info.bar1Total]
            })
            payload.append({
                'sensor_name': f"bar1_Used",
                'id': str(device_id),
                'value': bar1_info.bar1Used,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['bar1_Used'],
                'values': [bar1_info.bar1Used]
            })
            
            # Clock speeds
            try:
                graphics_clock = pynvml.nvmlDeviceGetClockInfo(handle, pynvml.NVML_CLOCK_GRAPHICS)
                memory_clock = pynvml.nvmlDeviceGetClockInfo(handle, pynvml.NVML_CLOCK_MEM)
                sm_clock = pynvml.nvmlDeviceGetClockInfo(handle, pynvml.NVML_CLOCK_SM)
            except:
                graphics_clock = memory_clock = sm_clock = 0
            payload.append({
                'sensor_name': f"graphics_clock",
                'id': str(device_id),
                'value': graphics_clock,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['graphics_clock'],
                'values': [graphics_clock]
            })
            payload.append({
                'sensor_name': f"memory_clock",
                'id': str(device_id),
                'value': memory_clock,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['memory_clock'],
                'values': [memory_clock]
            })
            payload.append({
                'sensor_name': f"sm_clock",
                'id': str(device_id),
                'value': sm_clock,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['sm_clock'],
                'values': [sm_clock]
            })

            # GPU utilization
            utilization = pynvml.nvmlDeviceGetUtilizationRates(handle)
            payload.append({
                'sensor_name': f"gpu_util",
                'id': str(device_id),
                'value': utilization.gpu,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['gpu_util'],
                'values': [utilization.gpu]
            })
            
            # Memory utilization
            payload.append({
                'sensor_name': f"mem_util",
                'id': str(device_id),
                'value': utilization.memory,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['mem_util'],
                'values': [utilization.memory]
            })
            
            # Temperature
            temp = pynvml.nvmlDeviceGetTemperature(handle, pynvml.NVML_TEMPERATURE_GPU)
            payload.append({
                'sensor_name': f"temp",
                'id': str(device_id),
                'value': temp,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['temp'],
                'values': [temp]
            })
            
            # Power usage
            power = pynvml.nvmlDeviceGetPowerUsage(handle) / 1000.0  # convert mW to W
            payload.append({
                'sensor_name': f"power",
                'id': str(device_id),
                'value': power,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['power'],
                'values': [power]
            })
            
            # Memory info
            memory = pynvml.nvmlDeviceGetMemoryInfo(handle)
            mem_used_mb = memory.used / (1024 * 1024)  # convert to MB
            payload.append({
                'sensor_name': f"mem_used",
                'id': str(device_id),
                'value': mem_used_mb,
                'device': device_name,
                'timestamp': timestamp,
                'measurements': ['mem_used'],
                'values': [mem_used_mb]
            })
        return payload
    
    def read_data(self):
        return self.get_sensor_data()


def read_data(sr):
    
    # get timestamp and data 
    timestamp = int(time.time()*1000)
    raw_packet = sr.sensor.get_sensor_data()
    
    # build the examon metric
    examon_data = []
    for raw_data in raw_packet:
        metric = {}
        metric['name'] = raw_data['sensor_name']
        metric['value'] = raw_data['value']
        metric['timestamp'] = timestamp
        metric['tags'] = sr.get_tags()
        metric['tags']['id'] = str(f"gpu_{raw_data['id']}")
        # build the final packet
        examon_data.append(metric)
        
    # worker id (string) useful for debug/log
    worker_id = sr.sensor.sensor_name
      
    return (worker_id, examon_data,)
        
                
def worker(conf, tags):
    """
        Worker process code
    """
    # sensor instance 
    sensor = Sensor()
    
    # SensorReader app
    sr = SensorReader(conf, sensor)
    
    # add read_data callback
    sr.read_data = read_data  
    
    # set the default tags
    sr.add_tags(tags)
    
    # run the worker loop
    sr.run()

   
if __name__ == '__main__':

    # start creating an Examon app
    app = ExamonApp()

    app.parse_opt()
    # for checking
    print("Config:")
    print(json.dumps(app.conf, indent=4))

    # set default metrics tags
    tags = app.examon_tags()
    tags['root']      = 'theta'
    tags['plugin']   = 'nvml_pub'
    # tags['chnl']     = 'data'
  
    # add a worker
    app.add_worker(worker, app.conf, tags)
    
    # run!
    app.run()

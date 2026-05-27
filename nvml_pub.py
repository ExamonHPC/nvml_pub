import json
import time
import random
from collections import OrderedDict
import pynvml  # Need to import pynvml for NVIDIA Management Library
from examon.plugin.examonapp import ExamonApp
from examon.plugin.sensorreader import SensorReader

# Import GPM extension module
try:
    import nvml_gpm_extension
    GPM_AVAILABLE = True
except ImportError as e:
    print(f"Warning: GPM extension not available: {e}")
    print("Build it with: python setup.py build_ext --inplace")
    GPM_AVAILABLE = False


class Sensor:
    def __init__(self, sensor_name='nvml_pub', range_min=0, range_max=100.0, device_id=None, metrics_to_monitor=None):
        self.sensor_name = sensor_name
        self.range_min = range_min
        self.range_max = range_max
        self.device_id = device_id  # None means all devices, otherwise single device
        self.metrics_to_monitor = metrics_to_monitor
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
        
        # Determine which devices to query
        if self.device_id is not None:
            # Single device mode
            device_range = [self.device_id]
        else:
            # All devices mode
            device_range = range(self.device_count)
        
        for device_id in device_range:
            handle = pynvml.nvmlDeviceGetHandleByIndex(device_id)
            device_name = f"{mqtt_tpc_dev}.gpu{device_id}"

            def add_metric(m_name, m_value):
                payload.append({
                    'sensor_name': m_name,
                    'id': str(device_id),
                    'value': m_value,
                    'device': device_name,
                    'timestamp': timestamp,
                    'measurements': [m_name],
                    'values': [m_value]
                })

            # GPU Performance Metrics using C extension
            if GPM_AVAILABLE:
                try:
                    gpm_metrics = nvml_gpm_extension.get_gpm_metrics(device_id)
                    # Add GPM metrics to payload if monitored
                    for metric_name, metric_value in gpm_metrics.items():
                        if self.metrics_to_monitor is None or metric_name in self.metrics_to_monitor:
                            add_metric(metric_name, metric_value)
                except Exception as e:
                    pass  # GPM not supported on this device


            # Performance state
            if self.metrics_to_monitor is None or 'perf_state' in self.metrics_to_monitor:
                perfstate = pynvml.nvmlDeviceGetPerformanceState(handle)
                add_metric("perf_state", perfstate)
            
            # BAR1 memory info
            if self.metrics_to_monitor is None or any(m in self.metrics_to_monitor for m in ['bar1_Total', 'bar1_Used', 'bar1_Free']):
                bar1_info = pynvml.nvmlDeviceGetBAR1MemoryInfo(handle)
                if self.metrics_to_monitor is None or 'bar1_Total' in self.metrics_to_monitor:
                    add_metric("bar1_Total", bar1_info.bar1Total)
                if self.metrics_to_monitor is None or 'bar1_Used' in self.metrics_to_monitor:
                    add_metric("bar1_Used", bar1_info.bar1Used)
                if self.metrics_to_monitor is None or 'bar1_Free' in self.metrics_to_monitor:
                    add_metric("bar1_Free", bar1_info.bar1Free)
            
            # Clock speeds
            if self.metrics_to_monitor is None or any(m in self.metrics_to_monitor for m in ['graphics_clock', 'memory_clock', 'sm_clock']):
                try:
                    graphics_clock = pynvml.nvmlDeviceGetClockInfo(handle, pynvml.NVML_CLOCK_GRAPHICS)
                    memory_clock = pynvml.nvmlDeviceGetClockInfo(handle, pynvml.NVML_CLOCK_MEM)
                    sm_clock = pynvml.nvmlDeviceGetClockInfo(handle, pynvml.NVML_CLOCK_SM)
                except:
                    graphics_clock = memory_clock = sm_clock = 0
                    
                if self.metrics_to_monitor is None or 'graphics_clock' in self.metrics_to_monitor:
                    add_metric("graphics_clock", graphics_clock)
                if self.metrics_to_monitor is None or 'memory_clock' in self.metrics_to_monitor:
                    add_metric("memory_clock", memory_clock)
                if self.metrics_to_monitor is None or 'sm_clock' in self.metrics_to_monitor:
                    add_metric("sm_clock", sm_clock)

            # GPU utilization
            if self.metrics_to_monitor is None or any(m in self.metrics_to_monitor for m in ['gpu_util', 'mem_controller_util']):
                utilization = pynvml.nvmlDeviceGetUtilizationRates(handle)
                if self.metrics_to_monitor is None or 'gpu_util' in self.metrics_to_monitor:
                    add_metric("gpu_util", utilization.gpu)
                if self.metrics_to_monitor is None or 'mem_controller_util' in self.metrics_to_monitor:
                    add_metric("mem_controller_util", utilization.memory)
            
            # Temperature
            if self.metrics_to_monitor is None or 'temp' in self.metrics_to_monitor:
                temp = pynvml.nvmlDeviceGetTemperature(handle, pynvml.NVML_TEMPERATURE_GPU)
                add_metric("temp", temp)
            
            # Power usage
            if self.metrics_to_monitor is None or 'power' in self.metrics_to_monitor:
                power = pynvml.nvmlDeviceGetPowerUsage(handle) / 1000.0  # convert mW to W
                add_metric("power", power)
            
            # Memory info
            if self.metrics_to_monitor is None or 'mem_used' in self.metrics_to_monitor:
                memory = pynvml.nvmlDeviceGetMemoryInfo(handle)
                mem_used_mb = memory.used / (1024 * 1024)  # convert to MB
                add_metric("mem_used", mem_used_mb)
        return payload
    
    def read_data(self):
        return self.get_sensor_data()

def _parse_mqtt_topic_to_tags(topic):
    """Build tags from MQTT_TOPIC in dotted key/value form."""
    tags = OrderedDict()
    if not topic:
        return tags

    parts = [p for p in topic.split('.') if p]
    if len(parts) >= 2 and len(parts) % 2 == 0:
        for idx in range(0, len(parts), 2):
            tags[parts[idx]] = parts[idx + 1]
    else:
        # Fallback: treat the whole topic as the root tag value.
        tags['root'] = topic

    return tags

def read_data(sr):
    # get timestamp and data 
    timestamp = int(time.time()*1000)
    
    t_start = time.time()
    raw_packet = sr.sensor.get_sensor_data()
    t_end = time.time()
    
    if sr.conf.get('LOG_LEVEL', 'INFO').upper() == 'DEBUG':
        sr.logger.debug(f"Retrieved {len(raw_packet)} metrics from NVML in {(t_end - t_start):.4f} seconds.")
    
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
                
def worker(conf, tags, device_id=None):
    """
        Worker process code
        If device_id is provided, this worker handles only that GPU
    """
    
    # read metrics from conf
    metrics_str = conf.get('METRICS', '')
    if metrics_str:
        metrics_to_monitor = [m.strip() for m in metrics_str.split(',') if m.strip()]
    else:
        metrics_to_monitor = None
        
    # sensor instance 
    sensor = Sensor(device_id=device_id, metrics_to_monitor=metrics_to_monitor)
    
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

    # set default metrics tags from config
    tags = app.examon_tags()
    tags.update(_parse_mqtt_topic_to_tags(app.conf.get('MQTT_TOPIC')))
    if 'plugin' not in tags:
        tags['plugin'] = 'nvml_pub'
    # tags['chnl']     = 'data'

    
    pynvml.nvmlInit()
    device_count = pynvml.nvmlDeviceGetCount()
    pynvml.nvmlShutdown()

    print(f"Detected {device_count} GPU(s). Creating one worker per GPU.")
    # Add one worker per GPU
    for device_id in range(device_count):
        app.add_worker(worker, app.conf, tags, device_id)
    
    # run!
    app.run()

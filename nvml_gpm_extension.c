/*
 * Python C Extension for NVML GPM Metrics
 * 
 * This module provides access to NVIDIA GPM (GPU Performance Metrics)
 * that are not yet available in the pynvml Python package.
 * 
 * to build: cd nvml_pub && python setup.py build_ext --inplace
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <nvml.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SAMPLE_INTERVAL_MS 150
#define MAX_METRICS 32

// GPM metrics to query
static const nvmlGpmMetricId_t GPM_METRICS[] = {
    NVML_GPM_METRIC_GRAPHICS_UTIL,
    NVML_GPM_METRIC_SM_UTIL,
    NVML_GPM_METRIC_SM_OCCUPANCY,
    NVML_GPM_METRIC_INTEGER_UTIL,
    NVML_GPM_METRIC_ANY_TENSOR_UTIL,
    NVML_GPM_METRIC_DFMA_TENSOR_UTIL,
    NVML_GPM_METRIC_HMMA_TENSOR_UTIL,
    NVML_GPM_METRIC_IMMA_TENSOR_UTIL,
    NVML_GPM_METRIC_DRAM_BW_UTIL,
    NVML_GPM_METRIC_FP64_UTIL,
    NVML_GPM_METRIC_FP32_UTIL,
    NVML_GPM_METRIC_FP16_UTIL,
    NVML_GPM_METRIC_PCIE_TX_PER_SEC,
    NVML_GPM_METRIC_PCIE_RX_PER_SEC,
};

#define GPM_METRICS_COUNT (sizeof(GPM_METRICS) / sizeof(GPM_METRICS[0]))

// Metric name mapping
static const char* get_metric_name(nvmlGpmMetricId_t metricId) {
    switch (metricId) {
        case NVML_GPM_METRIC_GRAPHICS_UTIL: return "graphics_util";
        case NVML_GPM_METRIC_SM_UTIL: return "sm_util";
        case NVML_GPM_METRIC_SM_OCCUPANCY: return "sm_occupancy";
        case NVML_GPM_METRIC_INTEGER_UTIL: return "integer_util";
        case NVML_GPM_METRIC_ANY_TENSOR_UTIL: return "any_tensor_util";
        case NVML_GPM_METRIC_DFMA_TENSOR_UTIL: return "dfma_tensor_util";
        case NVML_GPM_METRIC_HMMA_TENSOR_UTIL: return "hmma_tensor_util";
        case NVML_GPM_METRIC_IMMA_TENSOR_UTIL: return "imma_tensor_util";
        case NVML_GPM_METRIC_DRAM_BW_UTIL: return "dram_bw_util";
        case NVML_GPM_METRIC_FP64_UTIL: return "fp64_util";
        case NVML_GPM_METRIC_FP32_UTIL: return "fp32_util";
        case NVML_GPM_METRIC_FP16_UTIL: return "fp16_util";
        case NVML_GPM_METRIC_PCIE_TX_PER_SEC: return "pcie_tx_per_sec";
        case NVML_GPM_METRIC_PCIE_RX_PER_SEC: return "pcie_rx_per_sec";
        default: return "unknown";
    }
}

/*
 * Get GPM metrics for a specific GPU device
 * 
 * Args:
 *     device_index (int): GPU device index (0-based)
 * 
 * Returns:
 *     dict: Dictionary of metric_name -> value, or None on error
 */
static PyObject* get_gpm_metrics(PyObject* self, PyObject* args) {
    int device_index;
    nvmlReturn_t result;
    nvmlDevice_t device;
    nvmlGpmSupport_t gpmSupport;
    nvmlGpmSample_t sample1 = NULL;
    nvmlGpmSample_t sample2 = NULL;
    PyObject* metrics_dict = NULL;
    
    // Parse arguments
    if (!PyArg_ParseTuple(args, "i", &device_index)) {
        return NULL;
    }
    
    // Get device handle
    result = nvmlDeviceGetHandleByIndex(device_index, &device);
    if (result != NVML_SUCCESS) {
        PyErr_SetString(PyExc_RuntimeError, nvmlErrorString(result));
        return NULL;
    }
    
    // Check GPM support
    gpmSupport.version = NVML_GPM_SUPPORT_VERSION;
    result = nvmlGpmQueryDeviceSupport(device, &gpmSupport);
    if (result != NVML_SUCCESS || !gpmSupport.isSupportedDevice) {
        // Return empty dict if GPM not supported
        return PyDict_New();
    }
    
    // Allocate sample buffers
    result = nvmlGpmSampleAlloc(&sample1);
    if (result != NVML_SUCCESS) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to allocate sample1");
        return NULL;
    }
    
    result = nvmlGpmSampleAlloc(&sample2);
    if (result != NVML_SUCCESS) {
        nvmlGpmSampleFree(sample1);
        PyErr_SetString(PyExc_RuntimeError, "Failed to allocate sample2");
        return NULL;
    }
    
    // Get first sample
    result = nvmlGpmSampleGet(device, sample1);
    if (result != NVML_SUCCESS) {
        nvmlGpmSampleFree(sample1);
        nvmlGpmSampleFree(sample2);
        PyErr_SetString(PyExc_RuntimeError, "Failed to get sample1");
        return NULL;
    }
    
    // Wait for sample interval
    usleep(SAMPLE_INTERVAL_MS * 1000);
    
    // Get second sample
    result = nvmlGpmSampleGet(device, sample2);
    if (result != NVML_SUCCESS) {
        nvmlGpmSampleFree(sample1);
        nvmlGpmSampleFree(sample2);
        PyErr_SetString(PyExc_RuntimeError, "Failed to get sample2");
        return NULL;
    }
    
    // Prepare metrics structure
    nvmlGpmMetricsGet_t metricsGet;
    memset(&metricsGet, 0, sizeof(metricsGet));
    metricsGet.version = NVML_GPM_METRICS_GET_VERSION;
    metricsGet.sample1 = sample1;
    metricsGet.sample2 = sample2;
    metricsGet.numMetrics = GPM_METRICS_COUNT;
    
    // Set up metric IDs
    for (size_t i = 0; i < GPM_METRICS_COUNT; i++) {
        metricsGet.metrics[i].metricId = GPM_METRICS[i];
        metricsGet.metrics[i].nvmlReturn = NVML_ERROR_UNKNOWN;
    }
    
    // Query all metrics
    result = nvmlGpmMetricsGet(&metricsGet);
    
    // Create Python dictionary for results
    metrics_dict = PyDict_New();
    if (!metrics_dict) {
        nvmlGpmSampleFree(sample1);
        nvmlGpmSampleFree(sample2);
        return NULL;
    }
    
    if (result == NVML_SUCCESS) {
        // Add each successful metric to dictionary
        for (unsigned int i = 0; i < metricsGet.numMetrics; i++) {
            nvmlGpmMetric_t *metric = &metricsGet.metrics[i];
            
            if (metric->nvmlReturn == NVML_SUCCESS) {
                const char *metric_name = get_metric_name(metric->metricId);
                PyObject* value = PyFloat_FromDouble(metric->value);
                if (value) {
                    PyDict_SetItemString(metrics_dict, metric_name, value);
                    Py_DECREF(value);
                }
            }
        }
    }
    
    // Cleanup
    nvmlGpmSampleFree(sample1);
    nvmlGpmSampleFree(sample2);
    
    return metrics_dict;
}

/*
 * Check if GPM metrics are supported on a device
 * 
 * Args:
 *     device_index (int): GPU device index (0-based)
 * 
 * Returns:
 *     bool: True if GPM is supported, False otherwise
 */
static PyObject* is_gpm_supported(PyObject* self, PyObject* args) {
    int device_index;
    nvmlReturn_t result;
    nvmlDevice_t device;
    nvmlGpmSupport_t gpmSupport;
    
    if (!PyArg_ParseTuple(args, "i", &device_index)) {
        return NULL;
    }
    
    result = nvmlDeviceGetHandleByIndex(device_index, &device);
    if (result != NVML_SUCCESS) {
        Py_RETURN_FALSE;
    }
    
    gpmSupport.version = NVML_GPM_SUPPORT_VERSION;
    result = nvmlGpmQueryDeviceSupport(device, &gpmSupport);
    
    if (result == NVML_SUCCESS && gpmSupport.isSupportedDevice) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

// Module method definitions
static PyMethodDef NvmlGpmMethods[] = {
    {"get_gpm_metrics", get_gpm_metrics, METH_VARARGS,
     "Get GPM metrics for a GPU device. Returns dict of metric_name -> value."},
    {"is_gpm_supported", is_gpm_supported, METH_VARARGS,
     "Check if GPM metrics are supported on a device."},
    {NULL, NULL, 0, NULL}
};

// Module definition
static struct PyModuleDef nvml_gpm_module = {
    PyModuleDef_HEAD_INIT,
    "nvml_gpm_extension",
    "Python extension for NVML GPM metrics",
    -1,
    NvmlGpmMethods
};

// Module initialization
PyMODINIT_FUNC PyInit_nvml_gpm_extension(void) {
    return PyModule_Create(&nvml_gpm_module);
}

#!/usr/bin/python -u

import os
import json

BASE_PATH = "/run/obmc/cache_sensor_property"

def SetProperty(object_path, property_name, new_value):
    property_directory_path = BASE_PATH + object_path
    property_file_path = property_directory_path + "/sensor_property"
    if not os.path.isdir(property_directory_path):
        os.makedirs(property_directory_path)

    sensor_property = {}
    try:
        with open(property_file_path, "r") as readfile:
            sensor_property=json.load(readfile)
    except:
        pass
    sensor_property[property_name]  = new_value
    with open(property_file_path, 'w') as writefile:
        json.dump(sensor_property, writefile)

def GetProperty(object_path, property_name):
    property_directory_path = BASE_PATH + object_path
    property_file_path = property_directory_path + "/sensor_property"
    if not os.path.isfile(property_file_path):
        return -1
    sensor_property = {}
    with open(property_file_path, "r") as readfile:
        sensor_property=json.load(readfile)
    if property_name in sensor_property:
        return sensor_property[property_name]
    else:
        return -1

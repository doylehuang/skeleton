#!/usr/bin/env python
# -*- coding: utf-8 -*-

# pylint: disable=missing-docstring

from __future__ import print_function
import argparse
import logging
import sys
from obmc.events import Event
from obmc.events import EventManager

EVENT_LOG_SENSOR_NUMBER = 0x80

def create_log(args):
    event = Event.from_binary(
        args.severity,
        int(args.sensor_type, 16),
        int(args.sensor_number, 16),
        int(args.event_dir_type, 16),
        int(args.event_data_1, 16),
        int(args.event_data_2, 16),
        int(args.event_data_3, 16))
    record_id = EventManager().create(event)
    print('added log with record ID %04X' % record_id)

def build_parser():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()
    parser_add = subparsers.add_parser('add')
    parser_add.set_defaults(func=create_log)
    parser_add.add_argument(
        'severity',
        type=str,
        choices=[
            Event.SEVERITY_CRIT,
            Event.SEVERITY_OKAY,
            Event.SEVERITY_INFO,
            Event.SEVERITY_WARN])
    parser_add.add_argument(
        'sensor_type',
        type=str,
        help='sensor type in hexadecimal')
    parser_add.add_argument(
        'sensor_number',
        type=str,
        help='sensor number in hexadecimal')
    parser_add.add_argument(
        'event_dir_type',
        type=str,
        help='event_dir_type in hexadecimal')
    parser_add.add_argument(
        'event_data_1',
        type=str,
        help='event data 1 in hexadecimal')
    parser_add.add_argument(
        '--event_data_2',
        type=str,
        default='0xFF',
        help='event data 2 in hexadecimal')
    parser_add.add_argument(
        '--event_data_3',
        type=str,
        default='0xFF',
        help='event data 3 in hexadecimal')
    parser_clear = subparsers.add_parser('clear')
    parser_clear.set_defaults(func=clear_logs)
    parser_list = subparsers.add_parser('list')
    parser_list.set_defaults(func=list_logs)
    return parser

def clear_logs(_):
    event_manager = EventManager()
    event_manager.clear(EVENT_LOG_SENSOR_NUMBER)
    print('cleared all events')

def list_logs(_):
    event_manager = EventManager()
    events = event_manager.load_events()
    print('%04s | %19s | %8s | %s' % ('ID', 'TIME', 'SEVERITY', 'MESSAGE'))
    for event in events:
        print('%04s | %19s | %8s | %s' % (
            event.record_id,
            event.created,
            event.severity,
            event.message))

def main(argv):
    parser = build_parser()
    args = parser.parse_args(argv[1:])
    args.func(args)

# pylint: disable=invalid-name
if __name__ == '__main__':
    logging.basicConfig(format='%(levelname)-8s| %(message)s')
    logger = logging.getLogger('pyeventctl')
    logger.setLevel(logging.DEBUG)
    try:
        main(sys.argv)
    except StandardError as exce:
        logger.exception(exce)
# pylint: enable=invalid-name

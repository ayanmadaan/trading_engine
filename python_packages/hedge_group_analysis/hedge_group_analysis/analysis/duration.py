from dataclasses import dataclass
from datetime import datetime, timedelta
from hedge_group_analysis.utils import LogEntry, ColorManager
import json


Color = {
    'Red': '\033[31m',
    'Cyan': '\033[96m',
    'Green': '\033[92m',
    'Yellow': '\033[93m',
    'Orange': '\033[38;5;208m',
    'Purple': '\033[95m',
    'Reset': '\033[0m',
    'Gray': '\033[90m',
}

@dataclass
class Event:
    timestamp: datetime
    order_id: str
    name: str
    venue_role: str

class DurationAnalysis:
    def __init__(self):
        self.hedge_group = None
        self.events = []

    def load_entry(self, hedge_group: LogEntry):
        self.hedge_group = hedge_group

    def analyze(self):
        if self.hedge_group is None:
            raise ValueError("Hedge group not set")

        self.events = []
        json_string: str = self.hedge_group.message.split(" ")[2]
        json_object = json.loads(json_string)
        for order_id, order in json_object['orders'].items():
            venue_role = order['venue_role']

            # Add events
            for event, timestamp in order['events'].items():
                if timestamp != 'N/A':
                    self.events.append(Event(datetime.fromisoformat(timestamp), order_id, event, venue_role))

            # Add fills
            fill_id = 1
            for fill in order['fills']:
                self.events.append(Event(datetime.fromisoformat(fill['exchange_fill_time']), order_id, f"exchange_fill_time_{fill_id}", venue_role))
                self.events.append(Event(datetime.fromisoformat(fill['infra_notified_time']), order_id, f"infra_notified_time_{fill_id}", venue_role))
                self.events.append(Event(datetime.fromisoformat(fill['strategy_notified_time']), order_id, f"strategy_notified_time_{fill_id}", venue_role))
                fill_id += 1

        self.events.sort(key=lambda event: event.timestamp)

    @staticmethod
    def format_duration(td: timedelta) -> str:
        total_seconds = int(td.total_seconds())
        milliseconds = int((td.microseconds // 1000) % 1000)
        microseconds = int(td.microseconds % 1000)

        # Apply colors based on value
        seconds_color = Color['Gray'] if total_seconds == 0 else Color['Red']
        ms_color = Color['Gray'] if milliseconds == 0 else Color['Orange']
        us_color = Color['Gray'] if microseconds == 0 else Color['Yellow']

        result = ""
        result += f"{seconds_color}{total_seconds:3d} s{Color['Reset']} "
        result += f"{ms_color}{milliseconds:3d} ms{Color['Reset']} "
        result += f"{us_color}{microseconds:3d} us{Color['Reset']}"

        return result

    def report(self) -> None:
        previous_events = None
        color_manager = ColorManager()

        quote_ids = {}
        hedge_ids = {}

        for event in self.events:
            # Get consistent color for this order ID
            order_color = color_manager.get_color_for_order(event.order_id)

            # Format timestamp
            ts_str = event.timestamp.strftime('%Y-%m-%d %H:%M:%S.%f')

            # Calculate duration if we have a previous event for this order
            duration_str = " " * 19
            if previous_events is not None:
                duration = event.timestamp - previous_events.timestamp
                duration_str = f"{self.format_duration(duration)}"

            role = event.venue_role
            order_id = event.order_id
            role_id = -1

            if role == 'quote':
                if order_id not in quote_ids:
                    quote_ids[order_id] = len(quote_ids) + 1
                role_id = quote_ids[order_id]
            elif role == 'hedge':
                if order_id not in hedge_ids:
                    hedge_ids[order_id] = len(hedge_ids) + 1
                role_id = hedge_ids[order_id]

            # Print the event with consistent colors for order identification and timestamp
            print(f"{order_color}{role}_{role_id}{Color['Reset']} {order_color}{event.order_id}{Color['Reset']} | "
                f"{order_color}{ts_str}{Color['Reset']} | "
                f"{duration_str} | "
                f"{Color['Green']}{event.name}{Color['Reset']}")

            # Store this event for duration calculation of next event
            previous_events = event


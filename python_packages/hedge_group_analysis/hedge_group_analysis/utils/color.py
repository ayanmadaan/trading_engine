def get_order_colors():
    # Define a wide range of ANSI color codes (foreground colors ranging from 16-231)
    # These are the 6×6×6=216 colors in the color cube plus the 24 grayscale colors
    colors = []

    # Add some standard ANSI colors first for better visibility
    standard_colors = ['\033[31m', '\033[32m', '\033[33m', '\033[34m', '\033[35m', '\033[36m',
                      '\033[91m', '\033[92m', '\033[93m', '\033[94m', '\033[95m', '\033[96m']
    colors.extend(standard_colors)

    # Add 216 cube colors (skipping very dark and very light colors)
    for r in range(1, 5):  # red
        for g in range(1, 5):  # green
            for b in range(1, 5):  # blue
                # Calculate color number (16 + 36*r + 6*g + b)
                color_num = 16 + 36*r + 6*g + b
                colors.append(f'\033[38;5;{color_num}m')

    return colors

class ColorManager:
    def __init__(self):
        self.colors = get_order_colors()
        self.order_colors = {}
        self.color_index = 0

    def get_color_for_order(self, order_id):
        if order_id not in self.order_colors:
            # Assign next color
            self.order_colors[order_id] = self.colors[self.color_index % len(self.colors)]
            self.color_index += 1
        return self.order_colors[order_id]

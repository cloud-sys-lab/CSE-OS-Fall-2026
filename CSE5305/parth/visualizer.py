
import tkinter as tk
import threading
import queue
import time
import worker

from worker import start_workers, event_queue

# tkinter initialization
root = tk.Tk()
root.title("Thread Activity Visualizer")
root.geometry("900x1000")

# Global variables
thread_labels = {}
thread_history = {}
start_time = None
simulation_thread = None

# Upper UI elements:
# Title
title = tk.Label(
    root,
    text="Live Thread Activity Monitor",
    font=("Arial", 16, "bold")
)
title.pack(pady=15)
settings = tk.Frame(root)
settings.pack(pady=10)

# Thread Inputs
tk.Label(settings, text="Threads:").pack(side="left")
thread_input = tk.Spinbox(
    settings,
    from_=1,
    to=8,
    width=5
)
thread_input.delete(0, "end")
thread_input.insert(0, "4")
thread_input.pack(side="left", padx=10)

# Task Inputs
tk.Label(settings, text="Tasks:").pack(side="left")
task_input = tk.Spinbox(
    settings,
    from_=1,
    to=100,
    width=5
)
task_input.delete(0, "end")
task_input.insert(0, "12")
task_input.pack(side="left", padx=10)

# Lock checkbox
locking_enabled = tk.BooleanVar(value=True)
lock_checkbox = tk.Checkbutton(
    settings,
    text="Enable Locking",
    variable=locking_enabled
)
lock_checkbox.pack(side="left", padx=10)

# Sleep in lock checkbox
sleep_inside_lock = tk.BooleanVar(value=True)
sleep_lock_checkbox = tk.Checkbutton(
    settings,
    text="Sleep Inside Lock",
    variable=sleep_inside_lock
)
sleep_lock_checkbox.pack(side="left", padx=10)

# Workload settings frame
workload_settings = tk.Frame(root)
workload_settings.pack(pady=5)
tk.Label(workload_settings, text="Shared Access %:").pack(side="left")

# Lock probability input
lock_probability_input = tk.Spinbox(
    workload_settings, from_=0, to=100, width=5
)
lock_probability_input.pack(side="left", padx=5)
lock_probability_input.delete(0, "end")
lock_probability_input.insert(0, "50")

# Sleep probability input
tk.Label(workload_settings, text="Sleep %:").pack(side="left")
sleep_probability_input = tk.Spinbox(
    workload_settings, from_=0, to=100, width=5
)
sleep_probability_input.pack(side="left", padx=5)
sleep_probability_input.delete(0, "end")
sleep_probability_input.insert(0, "50")

# Shared delay input
tk.Label(workload_settings, text="Shared Delay (s):").pack(side="left")
delay_input = tk.Spinbox(
    workload_settings, from_=0, to=5,
    increment=0.5, width=5
)
delay_input.pack(side="left", padx=5)
delay_input.delete(0, "end")
delay_input.insert(0, "2")

# Seed input
tk.Label(workload_settings, text="Seed:").pack(side="left")
seed_input = tk.Entry(workload_settings, width=6)
seed_input.pack(side="left", padx=5)
seed_input.insert(0, "123")

# Frame for thread states
thread_frame = tk.Frame(root)
thread_frame.pack(pady=5)

# Visualizer functions
def create_thread_labels(num_threads):
    '''
    Creates labels for each thread to display their current state.
    '''
    for widget in thread_frame.winfo_children():
        widget.destroy()

    thread_labels.clear()
    thread_history.clear()

    for i in range(1, num_threads + 1):
        label = tk.Label(
            thread_frame,
            text=f"Thread {i}: Not Started",
            font=("Arial", 12),
            width=35,
            bg="lightgray",
            pady=8
        )

        label.pack(pady=4)

        thread_labels[i] = label
        thread_history[i] = []

def start_simulation():
    '''
    Starts the simulation by reading user inputs, initializing the GUI, and starting the worker threads.
    '''
    global start_time, simulation_thread

    try:
        num_threads = int(thread_input.get())
        num_tasks = int(task_input.get())
        lock_probability = int(lock_probability_input.get()) / 100
        sleep_probability = int(sleep_probability_input.get()) / 100
        shared_delay = float(delay_input.get())
        seed = int(seed_input.get())

        if not 0 <= lock_probability <= 1:
            raise ValueError("Invalid shared access probability")

        if not 0 <= sleep_probability <= 1:
            raise ValueError("Invalid sleep probability")

        if not 0 <= shared_delay <= 5:
            raise ValueError("Invalid shared delay")

    except ValueError as e:
        print(f"Invalid input: {e}")
        return

    if not (1 <= num_threads <= 8):
        return

    if not (1 <= num_tasks <= 100):
        return

    # Counter and expected counter initialization
    counter_label.config(text="Shared Counter: 0")
    expected_label.config(
        text=f"Expected Counter: {worker.expected_counter}"
    )
    result_label.config(text="")

    create_thread_labels(num_threads)

    canvas.config(height=num_threads * 40 + 70)

    start_time = time.perf_counter()

    start_button.config(state="disabled")
    thread_input.config(state="disabled")
    task_input.config(state="disabled")
    lock_checkbox.config(state="disabled")
    sleep_lock_checkbox.config(state="disabled")
    lock_probability_input.config(state="disabled")
    sleep_probability_input.config(state="disabled")
    delay_input.config(state="disabled")
    seed_input.config(state="disabled")

    simulation_thread = threading.Thread(
        target=start_workers,
        args=(
            num_threads,
            num_tasks,
            locking_enabled.get(),
            sleep_inside_lock.get(),
            seed,
            lock_probability,
            sleep_probability,
            shared_delay
        ),
        daemon=True
    )

    simulation_thread.start()

def reset_simulation():
    '''
    Resets simulation internal state and GUI
    '''
    global start_time

    start_time = None

    counter_label.config(text="Shared Counter: 0")
    expected_label.config(text="Expected Counter: 0")
    result_label.config(text="")
    performance_label.config(text="")

    # Clear all recorded thread histories
    for history in thread_history.values():
        history.clear()

    # Reset thread status labels
    for thread_id, label in thread_labels.items():
        label.config(
            text=f"Thread {thread_id}: Not Started",
            bg="lightgray"
        )

    # Clear the timeline
    canvas.delete("all")

    # Re-enable simulation controls
    start_button.config(state="normal")
    thread_input.config(state="normal")
    task_input.config(state="normal")
    lock_checkbox.config(state="normal")
    sleep_lock_checkbox.config(state="normal")
    reset_button.config(state="normal")
    lock_probability_input.config(state="normal")
    sleep_probability_input.config(state="normal")
    delay_input.config(state="normal")
    seed_input.config(state="normal")

def draw_timeline():
    '''
    Draws the timeline of thread activities on the canvas
    '''
    canvas.delete("all")

    if start_time is None:
        return

    colors = {
        "Waiting": "#EAB308",
        "Working": "#16A34A",
        "Finished": "#9CA3AF",
        "Blocked": "#DC2626",
        "Critical Section": "#9333EA",
        "Unprotected": "#EA580C",
        "Sleeping": "#2563EB"
    }

    scale = 70
    x_offset = 90
    row_height = 40

    current_time = time.perf_counter()

    # Freeze timeline when all threads finish
    if all(history and history[-1][0] == "Finished"
           for history in thread_history.values()):
        current_time = max(
            history[-1][2]
            for history in thread_history.values()
        )

    elapsed = current_time - start_time

    # Adjust scale if execution exceeds available space
    scale = min(scale, 740 / max(elapsed, 1))

    for thread_id, history in thread_history.items():

        y = 15 + (thread_id - 1) * row_height

        canvas.create_text(
            45, y + 12,
            text=f"Thread {thread_id}"
        )

        for i in range(len(history)):
            state, task_id, timestamp = history[i]

            if i + 1 < len(history):
                end_time = history[i + 1][2]
            else:
                end_time = current_time

            x1 = x_offset + (timestamp - start_time) * scale
            x2 = x_offset + (end_time - start_time) * scale

            canvas.create_rectangle(
                x1, y,
                x2, y + 25,
                fill=colors[state],
                outline="white",
                width=1
            )

            if task_id is not None and x2 - x1 > 35:
                canvas.create_text(
                    (x1 + x2) / 2,
                    y + 12,
                    text=f"T{task_id}",
                    fill="white"
                )

    # Draw time axis
    for second in range(int(elapsed) + 1):
        x = x_offset + second * scale

        axis_y = len(thread_history) * row_height + 15

        canvas.create_line(x, axis_y, x, axis_y + 5)
        canvas.create_text(x, axis_y + 17, text=f"{second}s")

def calculate_statistics():
    '''
    Statistics for comparison of thread methods and performance.
    '''
    total_blocked = 0
    total_critical = 0
    total_sleeping = 0
    lock_contentions = 0

    end_time = max(
        history[-1][2]
        for history in thread_history.values()
    )

    execution_time = end_time - start_time

    for history in thread_history.values():

        for i in range(len(history) - 1):

            state, task, timestamp = history[i]
            next_timestamp = history[i + 1][2]

            duration = next_timestamp - timestamp

            if state == "Blocked":
                total_blocked += duration
                lock_contentions += 1

            elif state == "Critical Section":
                total_critical += duration

            elif state == "Sleeping":
                total_sleeping += duration

    average_blocked = total_blocked / len(thread_history)

    performance_label.config(
        text=(
            f"Execution Time: {execution_time:.2f}s\n"
            f"Total Blocked Time: {total_blocked:.2f}s\n"
            f"Average Blocked Time: {average_blocked:.2f}s\n"
            f"Critical Section Time: {total_critical:.2f}s\n"
            f"Sleeping Time: {total_sleeping:.2f}s\n"
            f"Lock Contentions: {lock_contentions}"
        )
    )

def update_gui():
    '''
    Updates the GUI with the latest thread states and shared counter.
    '''
    try:
        while True:
            thread_id, state, task_id, timestamp = event_queue.get_nowait()
            thread_history[thread_id].append(
                (state, task_id, timestamp)
            )
            # print(thread_history[thread_id])

            colors = {
                "Waiting": "lightyellow",
                "Working": "lightgreen",
                "Finished": "lightgray",
                "Blocked": "#DC2626",
                "Critical Section": "#9333EA",
                "Unprotected": "#EA580C",
                "Sleeping": "#2563EB"
            }

            text = f"Thread {thread_id}: {state}"

            if task_id is not None:
                text += f" (Task {task_id})"

            thread_labels[thread_id].config(
                text=text,
                bg=colors[state]
            )

    except queue.Empty:
        pass

    # Update shared counter label
    if start_time is not None:
        counter_label.config(
            text=f"Shared Counter: {worker.shared_counter}"
        )

        expected_label.config(
            text=f"Expected Counter: {worker.expected_counter}"
        )

    # Check if all threads have finished and update the result label
    if (start_time is not None
            and simulation_thread is not None
            and not simulation_thread.is_alive()
            and thread_history):

        if all(
            history and history[-1][0] == "Finished"
            for history in thread_history.values()
        ):
            reset_button.config(state="normal")
            calculate_statistics()

            if worker.shared_counter == worker.expected_counter:
                result_label.config(
                    text="All increments preserved!",
                    fg="green"
                )
            else:
                lost = worker.expected_counter - worker.shared_counter

                result_label.config(
                    text=f"Race condition: {lost} lost updates!",
                    fg="red"
                )

    draw_timeline()

    root.after(100, update_gui)

# Bottom GUI elements:
# Start button
start_button = tk.Button(
    root,
    text="Start Simulation",
    font=("Arial", 12),
    command=start_simulation
)
start_button.pack(pady=20)

# Reset button
reset_button = tk.Button(
    root,
    text="Reset Simulation",
    font=("Arial", 12),
    command=reset_simulation,
    state="disabled"
)
reset_button.pack(pady=5)

# Timeline
canvas = tk.Canvas(
    root,
    width=850,
    height=230,
    bg="white"
)
canvas.pack(pady=10)

# Legend
legend = tk.Frame(root)
legend.pack(pady=5)
for state, color in [
    ("Working", "#16A34A"),
    ("Waiting", "#EAB308"),
    ("Finished", "#9CA3AF"),
    ("Blocked", "#DC2626"),
    ("Critical Section", "#9333EA"),
    ("Unprotected", "#EA580C"),
    ("Sleeping", "#2563EB")
]:
    tk.Label(
        legend,
        text=state,
        bg=color,
        width=12
    ).pack(side="left", padx=5)
settings.pack(pady=10)

# Statistics Frame
stats_frame = tk.Frame(root)
stats_frame.pack(pady=10)

# Shared Counter and Expected Counter Labels
counter_label = tk.Label(
    stats_frame,
    text="Shared Counter: 0",
    font=("Arial", 12)
)
counter_label.pack()
expected_label = tk.Label(
    stats_frame,
    text="Expected Counter: 0",
    font=("Arial", 12)
)
expected_label.pack()

# Result Label for success
result_label = tk.Label(
    stats_frame,
    text="",
    font=("Arial", 12, "bold")
)
result_label.pack()

# Statistics Label
performance_label = tk.Label(
    root,
    text="",
    font=("Arial", 11),
    justify="left"
)
performance_label.pack(pady=10)

update_gui()
root.mainloop()
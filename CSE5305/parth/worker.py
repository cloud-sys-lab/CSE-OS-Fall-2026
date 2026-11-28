import threading
import queue
import time
import random

resource_lock = threading.Lock()
shared_counter = 0
expected_counter = 0

task_queue = queue.Queue()        # Queue to hold tasks for threads
event_queue = queue.Queue()       # Queue to hold events for state updates

def update_state(thread_id, state, task_id=None):
    '''
    Changes the state of a thread and adds log to event_queue.
    '''
    timestamp = time.perf_counter()

    print(f"Thread {thread_id}: {state}, Task: {task_id}",
          flush=True)

    event_queue.put((thread_id, state, task_id, timestamp))

def simulate_work(duration):
    '''
    Simulates work being performed by a task
    '''
    end_time = time.perf_counter() + duration

    while time.perf_counter() < end_time:
        sum(i * i for i in range(1000))
        time.sleep(0.001)

def generate_tasks(num_tasks, seed=42, sleep_probability=0.5, lock_probability=0.5):
    '''
    Generate a list of tasks with random properties
    Able to configure sleep/lock probabilties
    '''
    rng = random.Random(seed)
    tasks = []

    for task_id in range(1, num_tasks + 1):
        task = {
            "id": task_id,
            "work_duration": rng.uniform(1, 2),
            "should_sleep": rng.random() < sleep_probability,
            "sleep_duration": rng.uniform(1, 2),
            "remaining_work": rng.uniform(0.5, 1),
            "requires_lock": rng.random() < lock_probability
        }

        tasks.append(task)

    return tasks

def worker(thread_id, use_lock, sleep_inside, shared_delay):
    '''
    Worker thread retrieves task from queue and completes it.
    Exits after retrieving a "None" task.
    '''

    global shared_counter
    while True:
        update_state(thread_id, "Waiting")

        task = task_queue.get()

        # Base case
        if task is None:
            task_queue.task_done()
            break

        task_id = task["id"]

        update_state(thread_id, "Working", task_id)
        simulate_work(task["work_duration"])
        if task["should_sleep"]:
            update_state(thread_id, "Sleeping", task_id)
            time.sleep(task["sleep_duration"])
            update_state(thread_id, "Working", task_id)
        simulate_work(task["remaining_work"])

        # Critical section: Only increment the shared counter for even-numbered tasks
        if task["requires_lock"]:

            if use_lock:
                if not resource_lock.acquire(blocking=False):
                    update_state(thread_id, "Blocked", task_id)
                    resource_lock.acquire()

            try:
                if use_lock:
                    update_state(thread_id, "Critical Section", task_id)
                else:
                    update_state(thread_id, "Unprotected", task_id)

                temp = shared_counter

                if sleep_inside:
                    time.sleep(shared_delay)

                shared_counter = temp + 1

            finally:
                if use_lock:
                    resource_lock.release()

            if not sleep_inside:
                update_state(thread_id, "Sleeping", task_id)
                time.sleep(shared_delay)

        task_queue.task_done()

    update_state(thread_id, "Finished")

def start_workers(num_threads=4, num_tasks=12, use_lock=True, sleep_inside=True, seed=123, 
                  lock_probability=0.5, sleep_probability=0.5, shared_delay=2):
    '''
    Increments shared counter using multiple workers.
    Generates tasks and starts worker threads to process them.
    '''
    global shared_counter, expected_counter
    shared_counter = 0
    threads = []

    for i in range(num_threads):
        thread = threading.Thread(
            target=worker,
            args=(i + 1, use_lock, sleep_inside, shared_delay)
        )
        threads.append(thread)
        thread.start()

    tasks = generate_tasks(
        num_tasks,
        seed=seed,
        lock_probability=lock_probability,
        sleep_probability=sleep_probability
    )

    expected_counter = sum(
        1 for task in tasks if task["requires_lock"]
    )
    print(tasks)

    for task in tasks:
        task_queue.put(task)

    task_queue.join()

    for _ in threads:
        task_queue.put(None)

    for thread in threads:
        thread.join()

    print(f"Final counter: {shared_counter}")
    print(f"Expected counter: {expected_counter}")
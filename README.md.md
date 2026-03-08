# Olympus Access Control System

A full-stack NFC-based access control system built with **ESP32-S3 + PN532 readers** and a **Django REST API** backend.
The system validates NFC cards against a central allowlist, logs all card scans, records successful entries and exits separately, and triggers gate opening for approved cards.

---

## Project Overview

**Olympus Access Control System** is a practical embedded + backend integration project designed for real-world access management.

The solution consists of two major parts:

- **Embedded layer (ESP32-S3)**
  - Reads NFC cards using **two PN532 readers**
  - Sends scanned UID data to the backend over Wi-Fi
  - Opens the gate/relay output when the backend approves access

- **Backend layer (Django + Django REST Framework)**
  - Receives card scan requests
  - Checks whether the card is allowed
  - Logs every attempt
  - Stores approved entries and exits in separate tables
  - Provides Django Admin for managing allowed cards and reviewing system activity

This project demonstrates practical skills in:

- Embedded programming with ESP32
- NFC-based device communication
- REST API design
- Backend logic with Django
- Data persistence with SQLite
- Admin interface design
- Deployment of a Python web application

---

## Key Features

- Two-reader NFC access flow
- Centralized backend validation
- Automatic gate open signal for approved cards
- Full logging of all card attempts
- Separate tracking for **entry** and **exit** events
- Web-based admin panel for card management
- REST API endpoints for device-server communication
- Timezone-aware timestamps configured for **Europe/Bratislava**

---

## System Architecture

```text
[NFC Card]
    ↓
[PN532 Reader 1 / Reader 2]
    ↓
[ESP32-S3]
    ↓  HTTP POST /api/check_card/
[Django REST API]
    ↓
[AllowedCard validation]
    ↓
 ┌───────────────┬──────────────────┐
 │ Allowed       │ Denied           │
 │ - Log event   │ - Log event      │
 │ - Save entry/exit │ - Return DENIED │
 │ - Return GATE_OPEN │              │
 └───────────────┴──────────────────┘
    ↓
[ESP32 opens gate output]
```

---

## How the System Works

### 1. Card scan
The ESP32 continuously listens on two PN532 NFC readers.

- **Reader 1** is used for **entry**
- **Reader 2** is used for **exit**

When a card is detected, the ESP32 converts the UID into an uppercase hex string format and sends it to the backend together with the reader number.

### 2. Backend validation
The Django API receives the request at:

```text
/api/check_card/
```

The backend:

1. Reads the `uid` and `reader`
2. Normalizes the UID
3. Checks whether the UID exists in the `AllowedCard` table
4. Logs the scan in `CardEvent`
5. If the card is approved:
   - Saves the event into `AllowedEntry` when `reader == 1`
   - Saves the event into `AllowedExit` when `reader == 2`
   - Returns `{"action": "GATE_OPEN"}`
6. If the card is not approved:
   - Returns `{"action": "DENIED"}`

### 3. Gate control
If the ESP32 receives `GATE_OPEN`, it activates the gate output pin for a fixed duration, then automatically closes it again.

### 4. Anti-duplicate logic
The firmware includes a cooldown mechanism to prevent repeated processing of the same card when it remains close to the reader.

---

## Technology Stack

### Embedded
- **ESP32-S3**
- **PN532 NFC modules**
- Arduino framework / `.ino` firmware
- Wi-Fi communication via HTTP

### Backend
- **Python**
- **Django 5.2.7**
- **Django REST Framework 3.16.1**
- **SQLite**
- Django Admin

---

## Repository Structure

```text
Olympus_-_Acces_Control_System/
│
├── api/                    # Django app containing API, models, serializers, views, admin config
├── esp_code/main/          # ESP32 firmware for NFC readers and gate control
├── nfcserver/              # Django project configuration
├── templates/              # Admin template customizations
├── manage.py               # Django management entry point
├── requirements.txt        # Python dependencies
└── db.sqlite3              # Local SQLite database
```

---

## Backend Documentation

### Django Project: `nfcserver`
This is the main Django project configuration.

Important files:

- `nfcserver/settings.py` – global Django settings, installed apps, database, timezone, templates
- `nfcserver/urls.py` – root routing configuration
- `nfcserver/wsgi.py` – WSGI entry point for deployment
- `manage.py` – command-line entry point for Django management tasks

### Django App: `api`
The `api` app contains the business logic of the access control system.

#### `models.py`
Defines the database structure.

##### `CardEvent`
Stores **every scanned card event**, regardless of whether access was allowed or denied.

Fields:
- `reader` – reader number
- `uid` – NFC card UID
- `timestamp` – automatic scan timestamp

##### `AllowedCard`
Stores the master list of cards that are permitted to access the system.

Fields:
- `uid` – unique NFC UID
- `owner_name` – optional owner name
- `is_allowed` – access flag

##### `AllowedEntry`
Stores approved **entry** events.
This is effectively a historical snapshot of approved cards scanned on **reader 1**.

Fields include:
- original card reference
- UID
- owner name
- original permission flag
- reader number
- admission timestamp

##### `AllowedExit`
Stores approved **exit** events.
This is the same concept as `AllowedEntry`, but for **reader 2**.

---

#### `serializers.py`
Converts Django model instances into JSON responses and validates incoming request data.

Implemented serializers:
- `CardEventSerializer`
- `AllowedEntrySerializer`
- `AllowedExitSerializer`

The serializers also format timestamps into a readable datetime string.

---

#### `views.py`
Contains the REST API logic.

##### `card_event(request)`
Accepts a POST request and directly stores a card event.

Endpoint:
```text
/api/card/
```

Purpose:
- Save raw card event data into the database

##### `list_events(request)`
Returns all card events ordered by newest first.

Endpoint:
```text
/api/events/
```

Purpose:
- Retrieve recent scan history

##### `check_card(request)`
This is the main business endpoint of the project.

Endpoint:
```text
/api/check_card/
```

Purpose:
- Validate a card UID
- Log the request
- Decide whether access should be granted
- Save successful entry/exit copies
- Return the gate action for the ESP32

Response examples:

```json
{"action": "GATE_OPEN"}
```

```json
{"action": "DENIED"}
```

---

#### `admin.py`
The Django Admin panel is customized for easier management and auditing.

Main admin behavior:
- `AllowedCard` can be searched and edited
- `CardEvent` is read-only in admin
- `AllowedEntry` is read-only in admin
- `AllowedExit` is read-only in admin
- Timestamps are formatted for readability

This creates a clean separation between:
- **editable allowlist data**
- **immutable audit logs**

---

#### `urls.py`
Defines API routes:

- `/api/card/`
- `/api/events/`
- `/api/check_card/`

---

## ESP32 Firmware Documentation

Location:

```text
esp_code/main/main.ino
```

The firmware is responsible for real-time NFC reading, Wi-Fi communication, and gate control.

### Main responsibilities

- Connect to Wi-Fi
- Initialize both PN532 readers
- Read card UIDs
- Avoid duplicate card reads
- Send card data to the server
- Parse JSON response from backend
- Trigger gate output on success
- Monitor Wi-Fi connection and reconnect if needed

### Core logic

#### Wi-Fi connection
The firmware starts in station mode and connects to the configured Wi-Fi network.
It also periodically checks the connection state and reconnects if the connection is lost.

#### Two-reader operation
The code handles two independent readers:
- **Reader 1** → entry
- **Reader 2** → exit

#### UID formatting
When a card is scanned, the UID is converted into a colon-separated uppercase hex string, for example:

```text
04:A1:BC:92
```

#### Server communication
The ESP32 sends a POST request containing:

```json
{
  "reader": 1,
  "uid": "04:A1:BC:92"
}
```

If the backend responds with `GATE_OPEN`, the firmware activates the output pin.

#### Gate state machine
The gate output is not latched permanently.
Instead, it is opened for a fixed amount of time and then automatically reset.

#### Duplicate-read prevention
The firmware stores the last card UID read on each reader and uses a cooldown interval so the same card is not processed repeatedly while still near the antenna.

---

## API Endpoints

### `POST /api/check_card/`
Main endpoint used by the ESP32.

**Request body**
```json
{
  "reader": 1,
  "uid": "04:A1:BC:92"
}
```

**Successful response**
```json
{
  "action": "GATE_OPEN"
}
```

**Denied response**
```json
{
  "action": "DENIED"
}
```

---

### `POST /api/card/`
Stores raw card event data.

---

### `GET /api/events/`
Returns all logged card events ordered by most recent first.

---

## Database Design

The database currently uses **SQLite**, which is appropriate for development and small-scale deployment.

### Main tables

- `AllowedCard` – allowlist / master card table
- `CardEvent` – full log of scan attempts
- `AllowedEntry` – approved entry log
- `AllowedExit` – approved exit log

### Design rationale
This design separates:
- operational permission data
- full audit history
- approved entry history
- approved exit history

That makes the system easier to audit and extend later.

---

## Django Admin Usage

The admin panel is used to:

- add or remove valid cards
- enable or disable card access
- search by UID or owner name
- inspect scan history
- review approved entry and exit records

This makes the project usable not only as a backend API, but also as a lightweight management application.

---

## Local Setup

### 1. Clone the repository
```bash
git clone https://github.com/tamascsiba/Olympus_-_Acces_Control_System.git
cd Olympus_-_Acces_Control_System
```

### 2. Create a virtual environment
```bash
python -m venv .venv
```

### 3. Activate the environment
**Windows**
```bash
.venv\Scripts\activate
```

**Linux/macOS**
```bash
source .venv/bin/activate
```

### 4. Install dependencies
```bash
pip install -r requirements.txt
```

### 5. Run migrations
```bash
python manage.py migrate
```

### 6. Create admin user
```bash
python manage.py createsuperuser
```

### 7. Start the server
```bash
python manage.py runserver
```

---

## Suggested Improvements

This project is already a strong practical prototype, but future improvements could include:

- move secrets and IPs to environment variables
- replace SQLite with PostgreSQL for production
- add authentication for API endpoints
- use HTTPS instead of plain HTTP for device communication
- add structured logging
- store deployment settings separately for development and production
- add tests for API validation logic
- add Docker support
- add CI/CD workflow for deployment

---

## Deployment

The backend is intended to be deployed on **AWS Elastic Beanstalk** as the production hosting environment.

For a production-ready Elastic Beanstalk deployment, the project should typically include:

- production settings
- environment variables for secrets
- proper `ALLOWED_HOSTS`
- a production WSGI server such as Gunicorn
- static file handling strategy
- database migration strategy

> At the time of documenting this repository, the source code clearly shows Django deployment structure (`manage.py`, `wsgi.py`) and a server IP used by the ESP32 client, but the repository itself does not visibly include a full Elastic Beanstalk configuration file set. If this application is already running on AWS Elastic Beanstalk, it is worth explicitly documenting the deployment flow and adding the deployment files to the repository as well.

---

## Why This Project Matters

This project is a strong portfolio example because it combines:

- embedded development
- network communication
- backend API design
- database modeling
- admin tooling
- real-world hardware/software integration
- deployment thinking

It demonstrates not only coding ability, but also system-level thinking across the full stack.

---

## Author

**Tamás Csiba**

GitHub: [tamascsiba](https://github.com/tamascsiba)

---

## License

You can add a license section here if you want to make the repository open-source under MIT or another license.


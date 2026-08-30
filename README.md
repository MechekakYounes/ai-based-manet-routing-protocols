# AI-Based Reliable Routing Protocol for MANETs

This repository contains the practical implementation and experimental evaluation of a **reliable routing protocol for Mobile Ad Hoc Networks (MANETs)** using **NS-3**.

The project is part of a thesis focused on improving routing reliability in highly dynamic wireless networks through the integration of **Artificial Intelligence techniques at the network-node level**.

## 📌 Project Overview

Mobile Ad Hoc Networks consist of wireless nodes that communicate without relying on fixed infrastructure. Due to node mobility, changing network topology, interference, congestion, and unreliable wireless links, maintaining efficient and reliable routes is a challenging problem.

This project aims to design, implement, and evaluate a routing protocol that uses AI-based decision-making to improve routing performance and reliability in dynamic MANET environments.

The protocol will be evaluated against established MANET routing protocols using controlled NS-3 simulations.

## 🎯 Objectives

The main objectives of this project are:

* Build a reproducible MANET simulation environment using NS-3.
* Establish baseline results using existing routing protocols.
* Investigate the effects of node mobility, network density, and wireless conditions on routing performance.
* Design and implement a new routing protocol.
* Integrate AI-based decision-making at the node level.
* Evaluate the proposed protocol under different network scenarios.
* Compare the proposed approach with conventional routing protocols.
* Analyze routing reliability and overall network performance.

## 🧪 Experimental Metrics

The experiments will focus on several network-performance indicators, including:

* **Packet Delivery Ratio (PDR)**
* **Packet Loss**
* **End-to-End Delay**
* **Throughput**
* **Routing Overhead**
* **Hop Count**
* **Network Lifetime**
* **Energy Consumption**, where applicable

These metrics will be used to determine whether the proposed approach provides measurable improvements over conventional routing strategies.

## 🏗️ Current Simulation Architecture

The initial simulation environment consists of:

```text
                MANET Simulation
                       │
                       ▼
                     NS-3
                       │
             ┌─────────┴─────────┐
             │                   │
         Mobile Nodes        Wireless Channel
             │                   │
             └─────────┬─────────┘
                       │
                    Routing
                       │
                     AODV
                       │
                  UDP Traffic
                       │
                       ▼
                Network Metrics
                       │
                       ▼
                Data Analysis
```

The current implementation establishes a basic mobile ad hoc network using **AODV** as the routing protocol. This baseline will be extended throughout the project.

## 🛠️ Technologies

* **NS-3** — Network simulation
* **C++** — Routing protocol and simulation implementation
* **Python** — Data processing, analysis, and visualization
* **Pandas** — Experimental data analysis
* **NumPy** — Numerical processing
* **Matplotlib** — Result visualization
* **Git/GitHub** — Version control and project management

Additional machine-learning libraries may be introduced depending on the final AI approach.

## 📂 Planned Repository Structure

```text
.
├── scratch/
│   ├── manet-aodv.cc
│   ├── baseline/
│   └── proposed/
│
├── analysis/
│   ├── data/
│   ├── scripts/
│   └── plots/
│
├── results/
│   ├── raw/
│   └── processed/
│
├── docs/
│
└── README.md
```

The structure will evolve as the experimental framework and proposed routing protocol are developed.

## 🚀 Getting Started

### Prerequisites

The project requires:

* Linux
* C++ compiler
* Python 3
* CMake
* Git
* NS-3

The current development environment is **Fedora Linux**.

### Clone the repository

```bash
git clone <repository-url>
cd <repository-directory>
```

### Build

From the NS-3 project directory:

```bash
./ns3 configure --enable-examples --enable-tests
./ns3 build
```

### Run the initial MANET simulation

```bash
./ns3 run manet-aodv
```

### Run NS-3 tests

```bash
./test.py
```

## 📊 Experimental Roadmap

The project is being developed incrementally:

### Phase 1 — Simulation Environment

* [x] Install and configure NS-3
* [x] Create initial MANET scenario
* [x] Configure wireless communication
* [x] Configure node mobility
* [x] Integrate AODV
* [x] Establish UDP communication

### Phase 2 — Baseline Evaluation

* [ ] Packet delivery measurement
* [ ] Packet-loss measurement
* [ ] Throughput measurement
* [ ] End-to-end delay measurement
* [ ] Routing-overhead measurement
* [ ] AODV evaluation under different scenarios
* [ ] OLSR and other baseline protocols

### Phase 3 — Proposed Routing Protocol

* [ ] Define routing architecture
* [ ] Define node-level information and routing features
* [ ] Implement routing mechanism
* [ ] Integrate AI-based decision-making
* [ ] Validate protocol correctness

### Phase 4 — Experimental Evaluation

* [ ] Mobility experiments
* [ ] Node-density experiments
* [ ] Network-load experiments
* [ ] Packet-loss/interference experiments
* [ ] Compare proposed protocol against baseline protocols
* [ ] Statistical analysis
* [ ] Generate thesis figures and tables

## 🔬 Research Direction

The central research question is how **AI-based decision-making can improve routing reliability in highly dynamic MANET environments**.

The proposed approach will investigate whether a node can use locally available network information to make better routing decisions than conventional routing mechanisms.

Potential information available to a node may include:

```text
Neighbor information
       │
       ├── Link quality
       ├── Node mobility
       ├── Neighbor count
       ├── Packet loss
       ├── Queue status
       ├── Route history
       └── Other network conditions
                    │
                    ▼
              AI Decision
                    │
                    ▼
             Route Selection
```

The exact features and AI methodology will be determined during the development and experimentation stages.

## 📈 Reproducibility

All experiments are intended to be reproducible.

Simulation parameters such as:

* Number of nodes
* Simulation duration
* Node mobility
* Simulation area
* Traffic rate
* Packet size
* Wireless configuration
* Routing protocol

will be exposed as configurable parameters whenever possible.

Experimental results will be stored separately from source code to facilitate comparison between different routing strategies and simulation scenarios.

## 🎓 Thesis Context

This repository supports the practical component of the thesis:

> **A Reliable Routing Protocol for Mobile Wireless Networks: An Artificial Intelligence-Based Approach**

The project investigates the application of AI techniques to routing in mobile wireless networks and evaluates the proposed approach through network simulation.

## 📜 License

This project is developed for academic and research purposes.

License information will be added as the project progresses.

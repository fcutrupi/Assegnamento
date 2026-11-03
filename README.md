# Simulatore di una cella di assemblaggio flessibile

Progetto C++17 che simula una piccola cella industriale. Il sistema modella il rilascio degli ordini, il prelievo dei componenti in base alla distinta base (BOM), una singola stazione di assemblaggio, un magazzino condiviso e una flotta di AGV eseguiti in thread indipendenti.

## Requisiti implementati

- Rilascio degli ordini in ordine cronologico FIFO, leggendo gli orari da `orders.txt`.
- Caricamento della distinta base e delle scorte iniziali da file di testo.
- Almeno 10 AGV, ciascuno gestito da un thread dedicato.
- Thread indipendente per la stazione di assemblaggio e code sincronizzate per ordini e missioni di trasporto.
- Accesso al magazzino protetto da mutex e registrazione degli eventi thread-safe.
- Consegna dei componenti prima dell'assemblaggio e trasporto del prodotto finito al magazzino.
- Produzione di `sim_log.txt`, `kpi_report.txt` e del riepilogo aggiuntivo `warehouse_summary.txt`.
- Chiusura ordinata dopo che tutti gli ordini rilasciati sono stati gestiti.

## Compilazione

Dalla cartella principale del progetto:

```bash
cmake -S . -B build
cmake --build build -j
```

Sono necessari un compilatore C++17, CMake 3.16 o successivo e la libreria standard per i thread della piattaforma.

## Esecuzione

Per eseguire lo scenario standard incluso:

```bash
./build/fas_simulator --input data/case01_basic --output output
```

Altri scenari disponibili:

```bash
./build/fas_simulator --input data/case02_insufficient_inventory --output output_case02
./build/fas_simulator --input data/case03_heavy_load --output output_case03 --agvs 10
```

Opzioni disponibili:

- `--input DIR`: cartella contenente `orders.txt`, `bom.txt` e `warehouse.txt`.
- `--output DIR`: cartella in cui scrivere log e report.
- `--agvs N`: numero di thread AGV; il valore deve essere almeno 10.
- `--scale-ms MS`: millisecondi reali corrispondenti a un minuto simulato. Il valore predefinito è 5 ms.
- `--help`: mostra la sintassi dei comandi.

## Formato dei file di ingresso

`orders.txt` contiene un ordine per riga:

```text
HH MM product_id [priority]
```

La priorità è facoltativa e viene letta per compatibilità, ma la politica richiesta implementata è FIFO. Gli ordini con lo stesso orario mantengono l'ordine in cui compaiono nel file.

`bom.txt` supporta due formati. Una riga con due campi definisce il tempo base di assemblaggio; una riga con tre campi definisce la quantità di un componente:

```text
P1 24
P1 C1 4
P1 C2 2
```

`warehouse.txt` contiene le scorte iniziali:

```text
C1 40
C2 35
```

## Ipotesi della simulazione

La consegna lascia aperti l'orizzonte temporale e i dettagli dei percorsi. La simulazione usa un orologio accelerato e prende il primo orario degli ordini come riferimento temporale iniziale. Il log mostra quindi l'ora del giorno corrispondente al primo ordine, avanzando secondo il tempo simulato.

Le durate dei trasporti e delle operazioni sono espresse in minuti simulati:

- Parcheggio → magazzino: 2 minuti.
- Prelievo di un componente: 1 minuto.
- Magazzino → stazione: 3 minuti.
- Scarico di un componente: 1 minuto.
- Viaggio verso la stazione per prelevare un prodotto finito: 4 minuti.
- Prelievo del prodotto finito: 1 minuto.
- Stazione → magazzino: 3 minuti.
- Deposito del prodotto finito: 1 minuto.
- Ritorno al parcheggio al termine di una missione: 2 minuti.
- Setup: 2 minuti per ciascun tipo distinto di componente nella BOM.

Le quantità richieste vengono verificate e riservate in modo atomico prima di creare le missioni AGV. Se anche un solo componente è insufficiente, l'ordine viene rifiutato senza consumare parzialmente le scorte. Per ogni unità fisica viene creata una missione di trasporto; più AGV possono quindi lavorare in parallelo per lo stesso ordine. La stazione assembla un solo ordine alla volta.

## Indicatori KPI

- **Lead time medio:** media di tempo di completamento meno tempo di rilascio effettivo, calcolata sugli ordini completati. Il completamento è registrato quando il prodotto finito è rientrato nel magazzino.
- **Utilizzo della stazione:** tempo totale dedicato alle operazioni di assemblaggio (tempo base più setup) diviso per l'orizzonte simulato.
- **Throughput:** ordini completati divisi per l'orizzonte simulato espresso in ore.
- **Utilizzo medio AGV:** somma dei tempi occupati di tutti gli AGV divisa per numero di AGV per orizzonte simulato.







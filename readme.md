## Nettverksprosjekt


## Hendelser
Alle hendelser som starter med "!" er reservert.

### Klient-server

| Hendelse | Beskrivelse               | Pakkeinhold                      |
|----------|---------------------------|----------------------------------|
| !ping    | Sender en ping til server | connection_id<br>client_timestamp |
| !connect | Lager en brukersesjon     | void                             |


### Server-klient
| Hendelse | Beskrivelse     | Pakkeinhold      |
|----------|-----------------|------------------|
| !ping    | Ping-respons    | client_timestamp |
| !connect | Connect-respons | connection_id    |

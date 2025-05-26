# Nettverksprosjekt
## Introduksjon
Dette prosjektet laget for IDATT2104 er et netcode-bibliotek, skrevet i c++ 23.
Biblioteket tar utgangspunkt i UDP-protokollen, og lar en utvikler sette opp både server og klient, begge basert på en hendelsesbasert modell.

## Implementert funksjonalitet
### Hendelsesbasert kommunikasjon
All data som sendes frem og tilbake, er hendelser, definert med en unik event-id.
Tilkoblingen mellom klient og tjener er tilstandsløs, og alle hendelser blir prosessert likt.
Det er derfor opp til en utvikler selv å definere server-tilstanden.
Alle klienter må derimot sende en oppkoblingsmelding og en ping hvert tiende sekund, for å kunne bli tilsendt hendelser.

Hendelser blir prosessert noen ganger i sekundet, etter en såkalt tick-rate. 
Om serveren har mye å prosessere, altså at tick_rate går ned, sender klienter automatisk mindre data.

### Interpolation/Reconciliation
Når klienten sender en melding til serveren, kan man enten velge å anta att oppdatering gikk gjennom, eller så kan an animere endringen i etterkant.

#### Reconciliation
Om man legger ved en hendelse, så gjøres det automatisk prediksjon med reconciliation.
Klienten antar at hendelsen blir akseptert av serveren, og den nye tilstanden blir umiddeltbart tilgjengelig.
Om serveren velger å forkaste hendelsen (mer om dette lenger ned), bytter klienten ut sin interne tilstand, med en som serveren har godkjent

#### Interpolation
Man kan velge å, istedenfor anta at hendelsen gikk gjennom, å istedenfor animere endringen mellom ulike tilstander fått av serveren.

## Installasjon
Prosjektet er laget med CMake, så installasjon burde gå ganske lett.
Allikevel er det et par ting som må gjøres.

### Avhengigheter
| Navn  | Beskrivelse                              | Minimumversjon |
|-------|------------------------------------------|----------------|
| CMake | Bygger prosjektet                        | 3.26           |
| SFML  | Grafikkbibliotek                         | 3.0.0          |
| Boost | Nettverksfunksjoner og diverse utilities | 1.88.0         |

### Installasjonsintruks
1. Last ned SFML fra https://www.sfml-dev.org/download/, og legg ved i en ny undermappe mappe "SFML".
2. Installer BOOST fra https://www.boost.org/releases/latest/, eller via en package manager.
3. Installer nlohmann/json via en package manager. Se https://github.com/nlohmann/json for mer informasjon.
4. Last ned Inter fra https://fonts.google.com/specimen/Inter. Hent font-filen (inter.ttf), og legg den i cmake-build-debug mappen (eller hvor du kjører prosjektet).

## Bruk
### Klient
En nettverksklient kan opprettes slik:

```c++
#include "client/netClient.cpp"
#include <boost/asio.hpp>


boost::asio::io_context event_loop(1);
NetClient client(event_loop, "localhost", 3000);
```

Clienten har tre argumenter:
1. Boost::io_context
2. Server-adresse
3. Server-port

For å kjøre serveren må man kjøre "start" metoden i clienten. Dette er lettest med co_spawn:
```c++
boost::asio::co_spawn(event_loop, client.start(), boost::asio::detached);
event_loop.run();
```

#### Opprette hendelser
Nettverksbiblioteket er avhengig av hendelser, så for at noe skal skje må dette legges til. På klienten ser dette slik ut:

```c++
NetClient client(...init);
auto event = client.add_event("eksempel", Events::Json());
```

> **OBS!** Alle hendelser må tilsvare en *ny* tilstand. Relative tilstander som "flytt deg 5 skritt til høyre", fungerer ikke, og vil bli overskrevet

#### Motta hendelser
Når en hendelse er lagt til, mottar hendelsen automatisk ny data:
```c++

auto current_data = event->get_current_data(); // inneholder den foreløpige verdien til hendelsen
auto latest_data = event->get_latest_data(); // den siste dataen mottatt fra server
```

Det er også mulig å hente ut data i form av et callback
```c++
auto event = client.add_event("eksempel", Events::Json([](const auto &data){
    std::cout << "eksempel mottat fra server med data " << data << std:::endl; 
}));
```

#### Sende hendelser
For å sende en hendelse, bruker man samme event-objekt som tidligere.
```c++
json message={
    {"melding", "hei"}
}; // lag et json objekt
event->send(message); // send objektet til server
```

#### Interpolation/Reconciliation
Alle hendelser som ikke er såkalte "one shots", altså hendelser som er ment å representere en kontunuerlig bevegelse, kan forutses med enten prediksjon eller interplasjon.
For å gjøre dette bruker man en spesiell event-klasse, som ligger under navneomerådet Events::Interpolated

```c++
// Gjør hendelsen på klienten først, så send til server
auto client_side_predicted_event = client.add_event("move", Events::Interpolated::Vector2f());

// Send til hendelsen til server først, så animer bevegelsen
auto client_side_interpolated_event = client.add_event("move", Events::Interpolated::Vector2f(Events::Interpolated::Interpolate));
```

I det første eksempelet, `client_side_predicted_event`, oppdaterer klienten først sin egen interne tilstand, før den nye oppdatering blir sendt til serveren. Helt til den får en respons, antas det at hendelsen ble akseptert.

I det andre derimot, sendes hendelsen først til serveren, før en eventuell oppdatering blir animert.

I begge tilfeller kan den "nyeste" verdien hentes ut med
```c++
auto interpolated_position = client_side_predicted_event->get_current_data(); // Denne verdien er imellom to bekreftede verdier fra serveren
auto predicted_position = client_side_interpolated_event->get_current_data(); // Er enten bekreftet av serveren, eller antas å være korrekt
```

### Egendefinerte hendelser
Vedlagt i bilioteket ligger det tre hendelser, Events::Json, Events::Vector2f og Events::Interpolated::Vector2f. For nærmest alle bruksomeråder er man nødt til opprette egendefinerte hendelser. Heldigvis er dette ikke vanskelig.

#### Ikke-predikerte ("vanlige") hendelser
Her trenger du kun å definere hvordan en hendelse skal kunne oversettes fram of tilbake mellom JSON.
Eksempelvis kan man gjøre noe slik:
```c++
#include "client/event.cpp"

struct min_hendelse{
    std::string melding;
}


class MinHendelse: public Event<min_hendelse>{
public:
    // hvordan oversetter man til json?
    Packet serialize(const sf::min_hendelse &hendelse) override {
        nlohmann::json data{
            {"melding", hendelse.melding},
        };

        return {this->event_id, data};
    }

    // hvordan henter man ut pakkeinnholdet?
     min_hendelse deserialize(const Packet &packet) override {
        return {
            packet.content["melding"]
        };
    }
};
}
```

Denne nye hendelsen kan da brukes som alle andre hendelser:
```c++
auto min_hendelse = client.add_event("hendelse", MinHendelse());
```

#### Predikerte hendelser
Predikerte hendelser er mer avansert, så det må defineres mye ekstra inforamasjon, for å kunne predikere nye verdier.
Hovedsakelig dreier dette seg om datatypen som det ønskes brukt. Her må følgene egenskaper defineres
1. En `.length()` funksjon som finner den totale "lengden" av datatypen
2. En `.noralized()` funskjon, som lager en normalisert verdi. (f.eks. en vektor innenfor rommet `[[-1, 1], [-1, 1]]`)
3. En metode for å plusse sammen to av datatypen
4. En metode for å minuse to av datatypen
5. En metode for å gane sammen datatypen og et flyttall.

Eksempel:

```c++

// sf::Vector2f inneholder alle egenskapene definert over, så her trengs det ikke noe ekstra setup
class Vector2f : public InterpolatedEventBase<sf::Vector2f> {
    public:
        Vector2f(): InterpolatedEventBase<sf::Vector2f>(sf::Vector2f(0, 0)) {} // her må vi legge til en "initial" verdi
        Vector2f(Events::Interpolated::ClientSidePredictToken token): InterpolatedEventBase<sf::Vector2f>(token, sf::Vector2f(0, 0)) {}
        
        // OBS: her bytter serialize metoden navn fra serialize til serialize_impl
        Packet serialize_impl(const sf::Vector2f &vec) override {
            nlohmann::json data{
                    {"x", vec.x},
                    {"y", vec.y}
            };
            return {this->event_id, data};
        }
        
        sf::Vector2f deserialize(const Packet &packet) override {
            return {
                    packet.content["x"],
                    packet.content["y"]
            };
        }
    };
```

### Server
Serveren mottar hendelser på litt annen måte enn klienten. Hendelelseshåndtering på serveren er tilstandsløs, noe som betyr at alle hendelser mottas via callbacks.
Alle server-hendelser burde oppdatere alle klienter, og det betyr at serveren har kontroll over hva slags respons som skal sendes tilbake til klienter, via to funksjoner; accept og reject.

```c++
server.add_event("move", ServerEvents::Vector2f([](const sf::Vector2f &data, const server_response_actions<sf::Vector2f> &actions){
    auto [accept, reject] = actions;
    
    accept(message); //Hendelsen er godkjent/riktig
    reject(message); //Hendelsen er ikke godkjent
}));
```
- Accept: Signaliserer til alle klienter at en klient har sendt en hendelse som er blitt godkjent av server. Klienter fortsetter som vanlig med prediksjon.
- Reject: Hendelsen er ikke blitt godkjent, og vedlagt ligger den siste "korrekte" server-tilstanden. Alle klienter bruker denne nye tilstanden, inkludert klienten som sendte hendelsen.

På lik måte som for klienthendelser, finnes det er par egendefinerte hendelser i biblioteket, Json og Vector2f.
For å lage nye hendelser, trenger man kun å implementere ServerEvent-klassen:

```c++
struct min_hendelse{
    std::string melding;
}

class MinHendelse : public ServerEvent<sf::Vector2f> {
public:
    MinHendelse(const std::function<void(const sf::Vector2f &data, const server_response_actions<sf::Vector2f> &actions)> &callback): ServerEvent<sf::Vector2f>(callback){}

    //OBS: her returneres det json, og ikke Packet
    json serialize(const min_hendelse &hendelse) override {
        nlohmann::json data{
            {"melding", hendelse.melding},
        };
        return data;
    }

    // helt lik som klienten!
    min_hendelse deserialize(const Packet &packet) override {
        return {
            packet.content["melding"]
        };
    }
};
```

Denne nye hendelsen kan brukes slik:

```c++
server.add_event("move", MinHendelse([](const min_hendelse &data, const server_response_actions<min_hendelse> &actions){
    ...
}));
```

### Reserverte hendelser
Alle hendelser som starter med "!" er reservert.

#### Klient-server:

| Hendelse | Beskrivelse               | Pakkeinhold                      |
|----------|---------------------------|----------------------------------|
| !ping    | Sender en ping til server | connection_id<br>client_timestamp |
| !connect | Lager en brukersesjon     | void                             |


#### Server-klient:
| Hendelse | Beskrivelse     | Pakkeinhold      |
|----------|-----------------|------------------|
| !ping    | Ping-respons    | client_timestamp |
| !connect | Connect-respons | connection_id    |

## Videre arbeid
Selv om biblioteket har mye funksjonalitet, er det fortsatt mye som kan forbedres. Under er et par utviklingsområder

### Utvide hendelses-validering
Når en klient sender en hendelse, sender den også med en hendelses-id. 
Denne id-en lagres på klienten, slik at den senere kan validere den.
Foreløpig er valideringen enkel, og sletter alle id-er som var før den om hendelsen ble akseptert, eller sletter alle om den ikke ble akseptert.
Her er det rom for å gjør mer avansert validering, f.eks. om hendelsen var sendt fra en annen klient.

### Sende samme hendelse fra to klienter
Dette punktet utvider litt på det over. 
At to klienter sender samme hendelse (f.eks. kontrollerer samme karakter), er utestet funksjonalitet.
Når klienten mottar en hendelse den ikke forventer å motta, gjør den foreløpig ingenting nytt.


### Ulike interpolasjons-typer
Biblioteket bruker for øyeblikket kun "spring interpolation". Det kan være hensiktsmessig å legge til andre ulike typer interpolasjon, som f.eks linjær eller spline.

### Relative hendelser
En vesentlig egenskap med løsningen er det som jeg har valgt å kalle "eventPool".
Like hendelser som sendes hyppig, samles, og kun *siste* blir faktisk sendt til serveren.
Dette betyr at alle hendelser kun kan representere en *ny* tilstand, og kan ikke være relative, som f.eks "beveg deg 5 steg til høyre".
En løsning her er enten å summere alle hendelsene (begrenser uvikler-implementasjon), eller å sende alle hendelsene i en stor liste (øker server-prosesseringstid).

### Ikke-broadcast hendelser
Når en hendelse enten aksepteres eller avslås av serveren, sendes den oppdaterte daten til *alle* klienter.
Dette gjør at serveren kan godkjenne f.eks. deler av forespørselen, istedenfor å forkaste absoultt hele den motatte pakken.
Desverre, fører dette til at hendelser som kun kan foregå mellom klient og tjener (f.eks. autentisering) ikke er mulig.

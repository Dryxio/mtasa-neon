# Bridge UI web ↔ client natif

Contrat entre le shell React (Main Menu + Server Browser) et `CServerBrowserWeb`
([Client/core/ServerBrowser/CServerBrowserWeb.cpp](../../Client/core/ServerBrowser/CServerBrowserWeb.cpp)).
Côté TypeScript, il est implémenté par
[src/backend/cef/CefBackend.ts](./src/backend/cef/CefBackend.ts) et simulé par
[src/backend/cef/devStub.ts](./src/backend/cef/devStub.ts) (`?cefsim` dans
l'URL — permet de développer/tester sans le client).

## Transport

- **JS → C++** : `window.mta.triggerEvent('menu:<commande>', ...args)` ou
  `window.mta.triggerEvent('sb:<commande>', ...args)` — le
  binding V8 standard du client (`CCefApp`), validé par le code d'auth IPC.
  Tous les arguments sont des **chaînes**.
- **C++ → JS** : le natif exécute `window.__neonMenu.emit([...])` et
  `window.__neonSB.emit([...])` avec des tableaux d'événements JSON (un
  `ExecuteJavascript` max par frame, événements groupés).
- La page est servie par le scheme local : `http://mta/local/index.html` →
  résolu vers `MTA/cef/serverbrowser/` sur disque (chemins d'assets relatifs,
  d'où `base: './'` dans vite.config.ts).

## Commandes (JS → C++)

### Main Menu

| Commande | Arguments | Effet |
|---|---|---|
| `menu:ready` | — | Demande l'état initial : contexte hors-jeu/en-jeu, langue, langues disponibles et Neon Identity. |
| `menu:resume` | — | Ferme le menu en jeu et rend le contrôle au gameplay. |
| `menu:disconnect` | — | Déconnecte du serveur courant, avec la confirmation native configurée par l'utilisateur. |
| `menu:quickConnect` | — | Déclenche le Quick Connect natif. |
| `menu:mapEditor` | — | Lance l'éditeur de cartes natif. |
| `menu:settings` | — | Ouvre Settings en CEGUI au-dessus du shell, puis restaure le web à la fermeture. |
| `menu:about` | — | Ouvre About/Credits en CEGUI au-dessus du shell. |
| `menu:identity` | — | Lance/annule la connexion Discord ou déconnecte Neon Identity. |
| `menu:sound` | `highlight`/`select`/`back`/`error` | Joue l'événement frontend GTA SA résident correspondant (IDs 3/1/2/4). |
| `menu:setLanguage` | `locale` | Valide la locale contre la liste MTA, puis applique le changement natif. |
| `menu:quit` | — | Déclenche la fermeture native du client. |

Le canal `window.__neonMenu` reçoit `init` (`inGame`, `locale`, `languages[]`,
`identity`, `translations`), puis les mises à jour incrémentales `context`
(`inGame`) et `identity`. `translations` associe les clés sémantiques React aux
catalogues natifs `client` et `main_menu` ; une clé absente conserve son texte
anglais embarqué. Les noms, descriptions et langues déclarés par les serveurs
ne sont volontairement pas traduits. Le contexte en jeu affiche uniquement
Resume, Settings, Disconnect et Quit au-dessus du monde assombri.

Le code du Server Browser est chargé dynamiquement à l'ouverture de sa route.
Avant ce premier accès, `CefBackend` n'existe pas, le registre n'est pas chargé
et aucun scan ASE n'est lancé.

### Server Browser

| Commande | Arguments | Effet |
|---|---|---|
| `sb:ready` | — | La page est chargée. Le natif charge le dernier registre valide, actualise le manifeste HTTPS, répond `init`/`favourites`, puis scanne uniquement ses endpoints. |
| `sb:setSource` | `source` | Change de vue (`internet`/`favourites`/`recent`) : `list-reset` + snapshot filtré du registre. |
| `sb:refresh` | — | Re-scanne la source courante. |
| `sb:connect` | `host`, `port`, `password` | Connexion. Serveur verrouillé sans mot de passe → `connect-password-required`. Sinon flux natif (boîte CONNECTING, erreurs, file d'attente en CEGUI par-dessus la vue). |
| `sb:favourite` | `host`, `port`, `"1"`/`"0"` | Ajoute/retire des favoris (persisté en coreconfig) → `favourites`. |
| `sb:openExternal` | `url` | Demande d'ouvrir un lien http(s) dans le navigateur système (confirmation native). |
| `sb:close` | — | Ferme le navigateur, retour au menu. |

## Événements (C++ → JS)

| Événement | Champs | Sens |
|---|---|---|
| `init` | `version`, `source` | Version ASE du client (comparaison de compatibilité) et source active. |
| `list-reset` | `source` | Vider la liste côté page (changement de source ou refresh). |
| `server` | `source`, `server` | Un serveur découvert/mis à jour (poussé par vagues pendant le scan, ≤ 60 par frame). |
| `progress` | `source`, `scanned`, `total` | Progression du scan. |
| `refresh-finished` | `source` | Scan terminé. |
| `favourites` | `keys[]` | Clés `"ip:port"` des favoris (état complet). |
| `connect-password-required` | `host`, `port`, `name` | Ouvrir la modale mot de passe (sans message d'erreur). |
| `connect-failed` | `code`, `message` | Échec pré-connexion (nick invalide, serials requis, adresse invalide). Les échecs réseau restent affichés par les dialogues natifs. |

### Forme d'un `server`

```json
{
  "id": "141.94.26.7:22003", "ip": "141.94.26.7", "port": 22003, "httpPort": 22005,
  "serverId": "blitz-production", "name": "MTA:SA Neon — Blitz",
  "tagline": "Open-world 1v1 vehicle pursuits",
  "description": "Escape your rival or stop them before time runs out.",
  "gameMode": "Blitz", "map": "San Andreas",
  "version": "1.6", "players": 262, "maxPlayers": 500, "ping": 22,
  "passworded": true, "serials": false, "verified": true,
  "state": "online", "favourite": false, "playerList": ["nando", "..."],
  "countries": ["GB", "FR", "BR"],
  "languages": ["English", "French", "Portuguese (Brazil)"],
  "links": []
}
```

`ping: -1` = pas (encore) de réponse. `state` ∈ `queued` / `online` /
`offline`. `verified: false` = compteur joueurs non vérifié (affiché `x / y *`
en ambre).

Le nom officiel, l'accroche, la description, les communautés, les langues et
les liens viennent du registre Neon versionné. Joueurs, ping, verrouillage,
version, mode, carte et liste des joueurs restent lus en direct via ASE. Ce
découpage permet d'enrichir Neon sans modifier le protocole ASE historique.

Favoris et Récents sont des vues filtrées de cette même liste enregistrée. Une
adresse arbitraire n'y apparaît jamais, mais Quick Connect reste libre. Le
dernier manifeste strictement valide est conservé dans le profil client ; une
panne du service ne provoque jamais un fallback vers le master public MTA.

## Reste à faire (hors phase 1)

- Router les erreurs de connexion réseau (mauvais mdp en cours de connexion,
  file d'attente serveur plein) vers la page au lieu des popups CEGUI.

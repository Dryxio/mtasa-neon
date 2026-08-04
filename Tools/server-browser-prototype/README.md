# Neon — Prototype de frontend

Refonte du Main Menu et du navigateur de serveurs de MTA Neon sous forme de
prototype web React + TypeScript + Vite. La direction reprend la discipline du
frontend GTA: San Andreas : load screens illustrés, lavis noir, accents crème
et bleu clair, Pricedown/SA Gothic/Bank Gothic, surfaces plates et navigation
clavier. Le prototype tourne dans un navigateur normal sans dépendre du build
C++/MTA, mais son modèle de données et son contrat de service restent calqués
sur le client natif pour préparer l'intégration CEF.

## Lancer le prototype

```sh
cd Tools/server-browser-prototype
npm install        # première fois uniquement
npm run dev        # http://localhost:5173
```

La racine affiche le Main Menu. `#/servers` ouvre directement le Server
Browser ; Browse Servers et Quick Connect effectuent cette transition depuis
le menu.

`?cefsim&ingame` simule le contexte Échap en session : le monde reste visible
derrière le lavis et seuls Resume, Settings, Disconnect et Quit sont proposés.

Au premier affichage de chaque session, le Main Menu choisit une des 14 images
de chargement GTA SA dans `public/loadscreens/`. Le Server Browser reprend la
même illustration afin de former une seule expérience continue. Pour la QA,
`?loadscreen=7` force par exemple `loadsc7`.

Vérifications : `npx tsc -b` (TypeScript strict), `npm run lint`,
`npm run build`.

Trois backends, sélection automatique dans `src/backend/index.ts` :
navigateur normal → mock ; `?cefsim` dans l'URL → CefBackend sur un bridge
simulé (test du protocole sans le client) ; dans le client MTA →
CefBackend sur le vrai bridge natif. Le protocole est documenté dans
[BRIDGE.md](./BRIDGE.md). `npm run build:client` construit et copie le
bundle vers `Bin/mta/cef/serverbrowser/`, le dossier servi par
`CServerBrowserWeb`. Si l'entrée du bundle ou CEF manque, le Main Menu CEGUI
historique reste le fallback automatique.

La CI Windows reconstruit ce bundle depuis un checkout propre avant
`compose_files`, puis l'installeur NSIS inclut explicitement le document CEF et
les 14 images natives de connexion. Le job échoue si `index.html`, un chunk
JavaScript/CSS, les polices ou l'une des deux séries de 14 load screens manque
dans les entrées de l'installeur final.

Astuce démo : le mot de passe des serveurs verrouillés est « neon » ; un
serveur plein laisse entrer à la 3ᵉ tentative (file d'attente simulée).

## Fonctionnalités

- **Recherche** : texte libre sur le nom et la description, `langs:fr` sur les
  langues, `@pseudo` pour trouver un joueur. Une
  adresse (`ip[:port]`, `mtasa://…`) + Entrée ou Connect = connexion directe.
- **Sources** : Neon Network / Favourites / Recent (menu déroulant). Toutes les
  vues restent limitées aux endpoints publiés par le registre officiel ; Quick
  Connect conserve volontairement la connexion directe vers une adresse libre.
- **Filtres** : masquer pleins, vides, verrouillés, autres versions, hors
  ligne.
- **Liste officielle et volontairement courte** : le client charge le registre
  public HTTPS de Neon Identity, conserve son dernier manifeste valide et
  interroge uniquement ces endpoints via ASE pour les données temps réel.
- **Détails intégrés** : description, mode, communautés, liens sociaux, langues,
  nombre de joueurs et action de connexion.
- **Clavier** : ↑/↓ naviguent, ↵ rejoint, Échap ferme/efface, taper au
  clavier focalise la recherche.
- **Audio** : les interactions utilisent les sons frontend résidents de GTA
  SA (highlight, select, back et error), via le bridge natif et sans assets
  audio supplémentaires.
- **Flux de connexion** : modale mot de passe (avec erreur), spinner,
  échec/réessai ; la réussite alimente l'historique.
- **Persistance** : localStorage dans le mock ; favoris, récents et historique
  natifs (`coreconfig.xml`) dans le client MTA.

## Architecture

```
src/
  Experience.tsx              Navigation légère Main Menu ↔ Server Browser
  MainMenu.tsx/.css           Menu, load screen GTA SA et navigation clavier
  types.ts                   Modèle ServerItem (miroir de CServerListItem) + parsing d'adresse
  backend/BrowserBackend.ts  Contrat UI ↔ source de données native ou simulée
  backend/mock/              Implémentation simulée (scan par vagues, connexion, localStorage)
  backend/index.ts           Point d'injection du backend (seul endroit qui connaît le mock)
  store.ts                   État applicatif (module singleton + useSyncExternalStore)
  search.ts                  Parsing des préfixes de recherche + filtres + tri
  components/                UI pure (liste virtualisée, détails, modales, menus…)
```

Règle de dépendance : les composants ne connaissent que le store ; le store
ne connaît que `BrowserBackend`. `CefBackend` adapte les événements du bridge
natif, tandis que `MockBackend` garde le prototype autonome dans un navigateur.
Le module `App` est importé paresseusement : le backend, le chargement du
registre et le scan natif ne démarrent qu'à l'ouverture effective du Server
Browser. Le natif télécharge
`https://identity.mta-neon.com/.well-known/neon-server-registry`, valide sa
forme, le met en cache puis construit une liste ASE dédiée. Il ne revient jamais
au master public MTA si le registre est indisponible.

## Notes pour l'intégration CEF

- Les polices Pricedown, SA Gothic, SA Header et Bank Gothic sont bundlées par
  Vite depuis `Shared/data/MTA San Andreas/MTA/cgui/`, sans dépendance réseau.
- Les contenus longs et les alphabets non couverts utilisent Arial/les polices
  système comme fallback Unicode.
- Les drapeaux sont des sprites SVG inline dérivés des codes ISO alpha-2 ; ils
  ne dépendent donc pas de la police emoji de Windows/CEF.
- Les serveurs peuvent annoncer une région principale (`country`) ou plusieurs
  régions d'accueil (`countries`).
- Le sélecteur expose les locales natives de MTA et applique réellement la
  langue choisie. Les chaînes propres au shell sont encore en anglais et
  devront être raccordées au gettext du client.

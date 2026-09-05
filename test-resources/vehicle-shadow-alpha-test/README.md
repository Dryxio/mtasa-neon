# Vehicle shadow alpha test

Mini-ressource serveur : les changements d'alpha sont synchronises pour tous les joueurs.

1. Demarrer `vehicle-shadow-alpha-test`.
2. Monter dans un vehicule puis taper `/vshadow 0` : observer son ombre.
3. Taper `/pshadow 0` pour masquer aussi le joueur et isoler l'ombre du vehicule.
4. Taper `/vshadow 255` pour comparer, puis sortir du vehicule.
5. A pied, retaper `/vshadow 0` puis `/vshadow 255` : le dernier vehicule teste reste selectionne.
6. Refaire avec les ombres volumetriques activees puis desactivees dans les reglages video, de jour et a proximite du vehicule.
7. `/shadowreset` restaure l'alpha initial du joueur et de son dernier vehicule teste.

Les commandes acceptent aussi tout alpha entier entre 0 et 255. L'arret de la
ressource restaure l'alpha initial de tous les elements modifies encore existants.
Les autres ressources qui modifient continuellement l'alpha peuvent interferer.
Cette ressource ne modifie ni la meteo, ni l'heure, ni les reglages des ombres.

Verification avant deploiement : syntaxe Lua et XML, puis egalite SHA-256 des fichiers copies.
Le correctif moteur a ete compile en Release|Win32 (Game SA, controle des hooks
reussi). Le testeur a confirme son fonctionnement en jeu le 5 septembre 2026 ;
les reglages et cas individuels de la matrice ci-dessus n'ont pas ete consignes.

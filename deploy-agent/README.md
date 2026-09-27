# Deploy Agent

Ein Update-System für **jede Website** auf deinem Server: Einmal installieren,
dann in jede Website **dieselbe Zeile** einbauen. Ein Klick auf „Update“ holt
den neuesten Stand von GitHub. Optional passiert das automatisch bei jedem Push.

- Egal welche Technik: HTML, PHP, WordPress, Node, Python …
- Neue Website hinzufügen = **ein Befehl**
- Besucher sehen den Button nicht, nur du (nach Login)
- Build-Schritte werden automatisch erkannt (`npm`, `composer`, `.deploy.sh`)
- Bestehende Websites werden übernommen. Vorher wird eine Sicherung angelegt.
- Braucht nur Python 3 und Git

```
 Website A ─┐                                    ┌─▶ /var/www/website-a
 Website B ─┼─ Update-Klick ─▶ deploy.domain.de ─┼─▶ /var/www/website-b
 GitHub ────┘  oder Push         (Deploy Agent)  └─▶ /var/www/…
```

## 1. Einmalig installieren (Hetzner, Debian/Ubuntu)

Vorher eine Subdomain, z. B. `deploy.deine-domain.de`, per DNS (A-Record)
auf die Server-IP zeigen lassen. Dann:

```bash
git clone https://github.com/yorumainvalo0-crypto/Operating.git
cd Operating/deploy-agent
sudo ./install.sh deploy.deine-domain.de
```

Das richtet alles ein: Git, Dienst, nginx und HTTPS (falls certbot installiert
ist). Am Ende wird der **Admin-Token** angezeigt, das ist dein Passwort.

## 2. Website hinzufügen (ein Befehl pro Website)

```bash
sudo deploy-agent add meine-seite https://github.com/USER/REPO.git \
  --domain meine-seite.de --path /var/www/meine-seite
```

- `--path` ist der Ordner, aus dem nginx/Apache die Website ausliefert
  (Standard: `/var/www/<name>`).
- `--domain` ist die Domain der Website. Darüber erkennt der Button, welche
  Website gemeint ist. Mehrfach möglich.
- `--branch` gibt den Branch an (Standard: `main`).
- `--no-auto` schaltet das automatische Update bei Push ab.

Der Befehl zeigt dir danach die Zeile für die Website und die Daten für den
GitHub-Webhook. Ein Neustart ist nicht nötig.

Der Benutzer `deploy` muss in den Ordner schreiben dürfen:

```bash
sudo chown -R deploy:www-data /var/www/meine-seite
```

Weitere Befehle: `deploy-agent list`, `sudo deploy-agent remove NAME`,
`sudo deploy-agent update NAME` (Update direkt im Terminal).

## 3. Button einbauen (in jede Website gleich)

Vor `</body>` einfügen:

```html
<script src="https://deploy.deine-domain.de/widget.js" defer></script>
```

**So benutzt du ihn:** Website mit `#deploy` am Ende öffnen, z. B.
`https://meine-seite.de/#deploy`, und Admin-Token eingeben. Ab dann siehst du
in diesem Browser unten rechts die Update-Box. Normale Besucher sehen nichts.

**Dashboard für alle Websites:** `https://deploy.deine-domain.de/`

## 4. Automatisch bei Push (optional)

Im GitHub-Repo unter **Settings → Webhooks → Add webhook** die Werte eintragen,
die `deploy-agent add` ausgegeben hat:

- Payload URL: `https://deploy.deine-domain.de/meine-seite/webhook`
- Content type: `application/json`
- Secret: (aus der Ausgabe)

## Was beim Update passiert

1. `git fetch` + `git reset --hard` auf den neuesten Stand des Branches.
   Dateien, die nicht im Repo sind (`.env`, Uploads …), bleiben erhalten.
2. Danach wird, falls vorhanden, eins davon ausgeführt:
   - `.deploy.sh` im Repo → wird ausgeführt (volle Kontrolle, z. B. Neustart)
   - sonst automatisch erkannt: `package.json` → `npm ci` + `npm run build`,
     `composer.json` → `composer install`, `requirements.txt` + `.venv` → `pip install`
   - nichts davon → statische Seite, fertig

Beispiel `.deploy.sh` für eine Node-App:

```bash
npm ci
npm run build
sudo systemctl restart meine-app
```

Für `sudo systemctl restart` eine Regel anlegen (`sudo visudo -f /etc/sudoers.d/deploy-agent`):

```
deploy ALL=(root) NOPASSWD: /usr/bin/systemctl restart meine-app
```

**Bestehende Website ohne Git:** Beim ersten Update wird der Ordner nach
`/var/lib/deploy-agent/backups/` gesichert und dann übernommen.

## Konfiguration

`/etc/deploy-agent/config.json`. Änderungen werden automatisch übernommen.
Pro Website kann man statt der Auto-Erkennung feste Befehle setzen:
`"post_deploy": ["npm ci", "npm run build"]`.

## Fehlersuche

```bash
sudo systemctl status deploy-agent
sudo journalctl -u deploy-agent -f
```

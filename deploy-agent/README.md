# Deploy Agent

Aktualisiert Websites auf dem Server mit dem neuesten Stand aus GitHub,
entweder **per Klick** auf einen Update-Button in der Website oder
**automatisch** nach jedem Push (GitHub-Webhook).

- Nur Python 3 (Standardbibliothek) und Git, keine weiteren Pakete
- Mehrere Projekte (Websites und Apps) gleichzeitig
- Nach dem Update können eigene Befehle laufen, z. B. `npm ci`, `npm run build`
  oder ein Neustart des Dienstes
- Push während eines laufenden Updates? Das nächste Update wird automatisch
  angehängt.

```
GitHub ──push──▶ Webhook ─┐
                          ├─▶ nginx /deploy/ ─▶ deploy-agent (127.0.0.1:9000)
Website-Button ──Klick────┘                        │  git fetch + reset
                                                   │  post_deploy-Befehle
                                                   ▼
                                              /var/www/<projekt>
```

## 1. Installation (Hetzner, Debian/Ubuntu)

```bash
git clone https://github.com/yorumainvalo0-crypto/Operating.git
cd Operating/deploy-agent
sudo ./install.sh
```

Das Skript installiert Git, legt den Benutzer `deploy` an, richtet den
systemd-Dienst ein und erzeugt `/etc/deploy-agent/config.json` mit
zufälligem **Admin-Token** und Webhook-Secrets. Den Token gibt es am Ende aus.

## 2. Projekte eintragen

`sudo nano /etc/deploy-agent/config.json`:

| Feld             | Bedeutung                                                     |
|------------------|---------------------------------------------------------------|
| `repo`           | GitHub-URL (öffentlich: `https://github.com/user/repo.git`)   |
| `branch`         | Branch, der live gehen soll (meist `main`)                    |
| `path`           | Zielordner auf dem Server, z. B. `/var/www/my-site`           |
| `auto_update`    | `true`: bei jedem Push automatisch aktualisieren              |
| `webhook_secret` | Secret für den GitHub-Webhook                                 |
| `post_deploy`    | Befehle nach dem Update (im Projektordner), z. B. Build       |

Danach: `sudo systemctl restart deploy-agent`

**Rechte:** Der Benutzer `deploy` muss in den Zielordner schreiben dürfen:

```bash
sudo mkdir -p /var/www/my-site && sudo chown -R deploy:deploy /var/www/my-site
```

Ist der Ordner leer, klont der Agent das Repo beim ersten Update selbst.
Ein bestehender Ordner ohne Git wird nicht angefasst. Dann den Ordner
leeren oder das Repo einmal selbst hineinklonen.

Soll `post_deploy` einen Dienst neu starten, eine sudo-Regel anlegen
(`sudo visudo -f /etc/sudoers.d/deploy-agent`):

```
deploy ALL=(root) NOPASSWD: /usr/bin/systemctl restart my-app
```

> Achtung: `git reset --hard` überschreibt lokale Änderungen an
> Dateien, die im Repo liegen. Dateien, die nicht im Repo sind (z. B.
> `.env`, Uploads), bleiben erhalten.

## 3. nginx

Inhalt von `nginx.example.conf` in den `server { … }`-Block der Website
einfügen, dann `sudo nginx -t && sudo systemctl reload nginx`.

## 4. Update-Button in die Website einbauen

```html
<script src="/deploy/widget.js" data-project="my-site" defer></script>
```

Das blendet unten rechts eine kleine Box mit **Update**-Button,
aktuellem Commit, Status und Log ein. Beim ersten Klick wird der
Admin-Token abgefragt und im Browser gespeichert.

Die Box an einer bestimmten Stelle anzeigen, z. B. auf einer Admin-Seite:

```html
<div id="deploy-box"></div>
<script src="/deploy/widget.js" data-project="my-site" data-target="#deploy-box" defer></script>
```

Tipp: Den Button nur auf einer Admin-Seite einbinden. Ohne Token passiert
beim Klick zwar nichts, aber Besucher müssen ihn auch nicht sehen.

## 5. Automatisches Update (GitHub-Webhook)

Im GitHub-Repo unter **Settings → Webhooks → Add webhook**:

- Payload URL: `https://deine-domain.de/deploy/my-site/webhook`
- Content type: `application/json`
- Secret: `webhook_secret` aus der Config
- Events: *Just the push event*

Wichtig: In der Config `"auto_update": true` setzen.

## API

| Methode | Pfad                    | Auth               | Zweck                      |
|---------|-------------------------|--------------------|----------------------------|
| GET     | `/widget.js`            | –                  | Button-Skript              |
| GET     | `/projects`             | Bearer-Token       | Projektliste               |
| GET     | `/<projekt>/status`     | Bearer-Token       | Commit, letzter Lauf, Log  |
| POST    | `/<projekt>/update`     | Bearer-Token       | Update starten             |
| POST    | `/<projekt>/webhook`    | GitHub-Signatur    | Webhook von GitHub         |

Update per Kommandozeile:

```bash
curl -X POST -H "Authorization: Bearer <TOKEN>" https://deine-domain.de/deploy/my-site/update
```

## Fehlersuche

```bash
sudo systemctl status deploy-agent
sudo journalctl -u deploy-agent -f
```

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <tmxlite/Map.hpp>
#include <tmxlite/ObjectGroup.hpp>
#include "SFMLOrthogonalLayer.hpp"

#include <vector>
#include <queue>
#include <stack>
#include <unordered_map>
#include <optional>
#include <cmath>
#include <algorithm>
#include <iostream>


constexpr float TILE_SIZE = 32.f;
constexpr float CASTLE_MAX_HP = 200.f;
constexpr int CASTLE_DAMAGE_PER_ENEMY = 50;
constexpr float MIN_VIEW_WIDTH = 600.f;
constexpr float MAX_VIEW_WIDTH = 1630.f;
constexpr float ZOOM_IN = 0.95f;
constexpr float ZOOM_OUT = 1.05f;
constexpr float PROJECTILE_SPEED = 360.f;

struct UpgradeNode
{
    std::string name;
    int cost;

    float dmgMul;
    float rangeAdd;
    float fireRateMul;

    std::vector<UpgradeNode*> children;
};


UpgradeNode upgradeRoot{
    "Base Tower", 0,
    1.f, 0.f, 1.f,
    {}
};

UpgradeNode rapidFire{
    "Rapid Fire", 75,
    1.f, 0.f, 0.7f,
    {}
};

UpgradeNode longRange{
    "Long Range", 60,
    1.f, 50.f, 1.f,
    {}
};

UpgradeNode heavyShot{
    "Heavy Shot", 120,
    1.6f, 0.f, 1.f,
    {}
};


void buildUpgradeTree()
{
    upgradeRoot.children = { &rapidFire, &longRange };
    rapidFire.children = { &heavyShot };
}


enum class GameState
{   
    Mainmenu,
    Playing,
    Inventory,
    Upgrade,
    Settings,
    GameOver
};


struct PathNode { sf::Vector2f pos; };
using Path = std::vector<PathNode>;


struct EnemyStats
{
    float maxHP;
    float speed;
    int bounty;
};

struct TowerStats
{
    float range;
    float fireRate;
    float damage;
    int level;
};

std::unordered_map<int, EnemyStats> enemyDB = {
    {0, {100.f, 80.f, 50}},
    {1, {250.f, 100.f, 50}},
    {2, {250.f, 120.f, 50}},
    {3, {250.f, 140.f, 50}}
};

std::unordered_map<int, TowerStats> baseTowerDB = {
    {0, {160.f, 0.6f, 30.f, 1}}
};

std::unordered_map<int, int> upgradeCost = {
    {1, 50},
    {2, 100},
    {3, 200}
};


struct EnemySpawn { int type; };

struct Wave
{
    std::queue<EnemySpawn> queue;
    float interval;
};

struct WaveManager
{
    std::vector<Wave> waves;
    std::size_t currentWave = 0;
    std::size_t pathCounter = 0;
    sf::Clock spawnClock;
};

struct Enemy
{
    sf::Sprite sprite;
    float hp;
    int type;
    const Path* path;
    std::size_t nodeIndex = 0;

    Enemy(const sf::Texture& tex, int t, const Path* p)
        : sprite(tex), hp(enemyDB[t].maxHP), type(t), path(p)
    {
        sf::FloatRect b = sprite.getLocalBounds();
        sprite.setOrigin(sf::Vector2f{ b.size.x / 2.f, b.size.y / 2.f });
        sprite.setPosition((*path)[0].pos);
    }
};

struct VolumeSlider
{
    sf::RectangleShape track;
    sf::CircleShape knob;

    float minX;
    float maxX;
    float value; 
    bool dragging = false;
};


struct Tower
{
    sf::Sprite sprite;
    sf::Clock fireClock;
    TowerStats stats;

    UpgradeNode* upgradeNode;

    Tower(const sf::Texture& tex, sf::Vector2f pos, const TowerStats& base)
        : sprite(tex), stats(base), upgradeNode(&upgradeRoot)
    {
        auto b = sprite.getLocalBounds();
        sprite.setOrigin(sf::Vector2f{ b.size.x / 2, b.size.y / 2 });
        sprite.setPosition(pos);
    }
};


struct Projectile
{
    sf::CircleShape shape;
    Enemy* target;
    float damage;

    Projectile(sf::Vector2f pos, Enemy* e, float dmg)
        : target(e), damage(dmg)
    {
        shape.setRadius(4.f);
        shape.setOrigin(sf::Vector2f{ 4.f, 4.f });
        shape.setFillColor(sf::Color::Yellow);
        shape.setPosition(pos);
    }
};


struct InventorySlot
{
    sf::RectangleShape box;
    std::optional<sf::Sprite> icon;
};

struct UpgradeUI
{
    sf::RectangleShape panel;
    sf::Text text;
    sf::Text button;
    Tower* selected = nullptr;

    explicit UpgradeUI(const sf::Font& f) : text(f), button(f) {}
};

bool applyUpgrade(Tower& tower, UpgradeNode* node, int& coins)
{
    if (!node) return false;
    if (coins < node->cost) return false;

    coins -= node->cost;

    tower.stats.damage *= node->dmgMul;
    tower.stats.range += node->rangeAdd;
    tower.stats.fireRate *= node->fireRateMul;

    tower.upgradeNode = node;
    tower.stats.level++;

    return true;
}


static sf::Vector2f toSF(const tmx::Vector2f& v)
{
    return sf::Vector2f{ v.x, v.y };
}

bool upgradeTower(Tower& tower, int& coins)
{
    int lvl = tower.stats.level;
    auto it = upgradeCost.find(lvl);
    if (it == upgradeCost.end()) return false;
    if (coins < it->second) return false;

    coins -= it->second;
    tower.stats.level++;
    tower.stats.damage *= 1.35f;
    tower.stats.range += 25.f;
    tower.stats.fireRate *= 0.9f;
    return true;
}


int main()
{
    buildUpgradeTree();
    sf::RenderWindow window;
    window.create(sf::VideoMode({ 1920, 1080 }), "Siege of Lothal - Tower Defense game");
    window.setVerticalSyncEnabled(true);

    sf::Clock consoleClock;

    sf::Font font;
    font.openFromFile("D:/AI PROJECTS/C++/seige-of-lothal-main/x64/Debug/DejaVuSans.ttf");

    tmx::Map map;
    map.load("C:/Users/Rafi Sojan S/Downloads/map-final-2..tmx");

    sf::Text coinText(font);
    coinText.setCharacterSize(20);
    coinText.setPosition(sf::Vector2f{ 20.f, 60.f });

    MapLayer layer0(map, 0);
    MapLayer layer1(map, 1);

    sf::View mapView(sf::FloatRect(
        sf::Vector2f{ 0.f, 0.f },
        sf::Vector2f{ 1920.f, 1080.f }
    ));
    mapView.setCenter(sf::Vector2f{ 960.f, 540.f });

    sf::Texture backgroundimage;
    if (!backgroundimage.loadFromFile("C:/Users/Rafi Sojan S/Downloads/war-field-in-digital-alam6alejuznqq6t.jpg")) {
        std::cerr << "failed" << std::endl;
        return EXIT_FAILURE;
    }

    sf::Texture towerTex, enemyTex;
    towerTex.loadFromFile("C:/Users/Rafi Sojan S/Downloads/Archer-Upgrade-3-ezgif.com-crop.png");
    enemyTex.loadFromFile("C:/Users/Rafi Sojan S/Downloads/free-archer-towers-pixel-art-for-tower-defense/3 Units/1/U_Preattack.png");


    std::vector<Path> paths;
    for (const auto& layer : map.getLayers())
    {
        if (layer->getType() != tmx::Layer::Type::Object) continue;
        for (const auto& obj :
            layer->getLayerAs<tmx::ObjectGroup>().getObjects())
        {
            if (obj.getName().rfind("Path", 0) == 0)
            {
                Path p;
                sf::Vector2f base = toSF(obj.getPosition());
                for (const auto& pt : obj.getPoints())
                    p.push_back({ base + toSF(pt) });
                paths.push_back(p);
            }
        }
    }

   



    WaveManager waveMgr;
    Wave w1, w2, w3, w4;
    for (int i = 0; i < 5; ++i)
        w1.queue.push({ 0 });
    w1.interval = 4.5f;
    for (int i = 0; i < 10; ++i)
        w2.queue.push({ 1 });
    w2.interval = 4.5f;
    for (int i = 0; i < 20; ++i)
        w3.queue.push({ 2 });
    w3.interval = 4.5f;
    for (int i = 0; i < 20; ++i)
        w4.queue.push({ 3 });
    w4.interval = 4.5f;
    waveMgr.waves.push_back(w1);
    waveMgr.waves.push_back(w2);
    waveMgr.waves.push_back(w3);
    waveMgr.waves.push_back(w4);

    int coins = 100;

    std::stack<GameState> states;
    states.push(GameState::Playing);
    states.push(GameState::Mainmenu);
    
    InventorySlot slot;
    slot.box.setSize(sf::Vector2f{ 48.f, 48.f });
    slot.box.setPosition(sf::Vector2f{ 20.f, 20.f });
    slot.box.setFillColor(sf::Color(40, 40, 50));
    slot.box.setOutlineThickness(2.f);
    slot.box.setOutlineColor(sf::Color::White);

    float castleHP = CASTLE_MAX_HP;

    sf::RectangleShape castleHPBg({ 200.f, 14.f });
    castleHPBg.setPosition({ 20.f, 90.f });
    castleHPBg.setFillColor(sf::Color(120, 0, 0));

    sf::RectangleShape castleHPFg({ 200.f, 14.f });
    castleHPFg.setPosition(castleHPBg.getPosition());
    castleHPFg.setFillColor(sf::Color::Green);

    sf::Text gameOverText(font);
    gameOverText.setString("GAME OVER");
    gameOverText.setCharacterSize(72);
    gameOverText.setFillColor(sf::Color::Red);
    gameOverText.setPosition({ 640.f, 360.f });

    sf::Text waveText(font);
    waveText.setCharacterSize(20);
    waveText.setPosition({ 1000.f, 70.f });
    waveText.setFillColor(sf::Color::White);

    sf::Text towerCountText(font);
    towerCountText.setCharacterSize(20);
    towerCountText.setFillColor(sf::Color::White);
    towerCountText.setPosition({ 1000.f, 30.f });

    sf::Text restartText(font);
    restartText.setString("Press R to Restart\nPress ESC to Quit");
    restartText.setCharacterSize(28);
    restartText.setPosition({ 650.f, 460.f });

    VolumeSlider volumeSlider;

    
    volumeSlider.track.setSize({ 300.f, 6.f });
    volumeSlider.track.setFillColor(sf::Color(180, 180, 180));
    volumeSlider.track.setPosition({ 810.f, 380.f });

   
    volumeSlider.knob.setRadius(10.f);
    volumeSlider.knob.setOrigin({ 10.f, 10.f });
    volumeSlider.knob.setFillColor(sf::Color::White);
    volumeSlider.knob.setPosition({ 960.f, 383.f });

    
    volumeSlider.minX = volumeSlider.track.getPosition().x;
    volumeSlider.maxX = volumeSlider.minX + volumeSlider.track.getSize().x;

    
    volumeSlider.value = 50.f;


    sf::Music bgMusic;
    bgMusic.openFromFile("C:/Users/Rafi Sojan S/Downloads/19. The End.ogg");
    bgMusic.setLooping(true);
    bgMusic.play();
    bgMusic.setVolume(volumeSlider.value);

    UpgradeUI upgradeUI(font);
    upgradeUI.panel.setSize(sf::Vector2f{ 260.f, 180.f });
    upgradeUI.panel.setPosition(sf::Vector2f{ 20.f, 100.f });
    upgradeUI.panel.setFillColor(sf::Color(30, 30, 40, 230));
    upgradeUI.panel.setOutlineThickness(2.f);
    upgradeUI.panel.setOutlineColor(sf::Color::White);

    upgradeUI.text.setCharacterSize(16);
    upgradeUI.text.setPosition(upgradeUI.panel.getPosition() + sf::Vector2f{ 10.f, 10.f });

    upgradeUI.button.setString("UPGRADE");
    upgradeUI.button.setCharacterSize(18);
    upgradeUI.button.setPosition(
        upgradeUI.panel.getPosition() + sf::Vector2f{ 70.f, 130.f }
    );

    sf::Text castleText(font);
    castleText.setString("Castle HP");
    castleText.setCharacterSize(14);
    castleText.setPosition({ 20.f, 70.f });
    window.draw(castleText);

    sf::Text titleText(font);
    titleText.setString("SIEGE OF LOTHAL");
    titleText.setCharacterSize(72);
    titleText.setPosition({ 520.f, 200.f });

    sf::Text playText(font);
    playText.setString("PLAY");
    playText.setCharacterSize(40);
    playText.setPosition({ 860.f, 360.f });

    sf::Sprite backgroundimo(backgroundimage);

    sf::Text settingsText(font);
    settingsText.setString("SETTINGS");
    settingsText.setCharacterSize(40);
    settingsText.setPosition({ 820.f, 430.f });

    sf::Text quitText(font);
    quitText.setString("QUIT");
    quitText.setCharacterSize(40);
    quitText.setPosition({ 860.f, 500.f });

    sf::Text settingsTitle(font);
    settingsTitle.setString("SETTINGS");
    settingsTitle.setCharacterSize(56);
    settingsTitle.setPosition({ 720.f, 240.f });

    sf::Text backText(font);
    backText.setString("BACK");
    backText.setCharacterSize(36);
    backText.setPosition({ 860.f, 500.f });

    sf::Text enemiesalive(font);
    enemiesalive.setCharacterSize(20);
    enemiesalive.setPosition({ 1000.f, 50.f });
    enemiesalive.setFillColor(sf::Color::White);

    slot.icon.emplace(towerTex);
    sf::FloatRect ib = slot.icon->getLocalBounds();
    float sc = 40.f / std::max(ib.size.x, ib.size.y);
    slot.icon->setScale(sf::Vector2f{ sc, sc });
    slot.icon->setOrigin(sf::Vector2f{ ib.size.x / 2.f, ib.size.y / 2.f });
    slot.icon->setPosition(
        slot.box.getPosition() + slot.box.getSize() / 2.f);

    std::vector<Enemy> enemies;
    std::vector<Tower> towers;
    std::vector<Projectile> projectiles;
    std::optional<sf::Sprite> ghost;

    sf::CircleShape rangePreview;
    rangePreview.setFillColor(sf::Color(50, 200, 50, 40));
    rangePreview.setOutlineThickness(2.f);
    rangePreview.setOutlineColor(sf::Color(50, 200, 50, 120));

    sf::Clock dtClock;

    

    while (window.isOpen())
    {
        float dt = dtClock.restart().asSeconds();

        coinText.setString("Coins: " + std::to_string(coins));

        enemiesalive.setString("Enemies alive: " + std::to_string(enemies.size()));

        float ratio = std::max(0.f, castleHP) / CASTLE_MAX_HP;
        castleHPFg.setSize({ 200.f * ratio, 14.f });

        if (waveMgr.currentWave < waveMgr.waves.size())
        {
            waveText.setString(
                "Wave: " + std::to_string(waveMgr.currentWave + 1) +
                " / " + std::to_string(waveMgr.waves.size())
            );
        }
        else
        {
            waveText.setString("All Waves Cleared");
        }

        towerCountText.setString(
            "Towers Deployed: " + std::to_string(towers.size())
        );

        while (auto ev = window.pollEvent())
        {
            if (ev->getIf<sf::Event::Closed>())
                window.close();


            
            if (states.top() == GameState::GameOver)
            {
                if (auto k = ev->getIf<sf::Event::KeyPressed>())
                {
                    if (k->code == sf::Keyboard::Key::R)
                    {
                        enemies.clear();
                        towers.clear();
                        projectiles.clear();

                        waveMgr.currentWave = 0;
                        waveMgr.pathCounter = 0;
                        coins = 100;
                        castleHP = CASTLE_MAX_HP;

                            states.pop();
                            states.push(GameState::Playing);
                    }
                    if (k->code == sf::Keyboard::Key::Escape)
                    {
                        window.close();
                    }
                }
            }

            if (states.top() == GameState::Inventory)
            {
                if (auto m = ev->getIf<sf::Event::MouseButtonPressed>())
                {
                    sf::Vector2f pos =
                        window.mapPixelToCoords(m->position, window.getDefaultView());

                    if (backText.getGlobalBounds().contains(pos))
                    {
                        states.pop();
                    }
                }
            }
            
            if (states.top() == GameState::Mainmenu)
            {
                if (auto m = ev->getIf<sf::Event::MouseButtonPressed>())
                {
                    sf::Vector2f pos =
                        window.mapPixelToCoords(m->position, window.getDefaultView());

                    if (playText.getGlobalBounds().contains(pos))
                    {
                        states.pop();
                        states.push(GameState::Playing);
                    }
                    else if (settingsText.getGlobalBounds().contains(pos))
                    {
                        states.push(GameState::Settings);
                    }
                    else if (quitText.getGlobalBounds().contains(pos))
                    {
                        window.close();
                    }
                }
            }

            if (states.top() == GameState::Settings)
            {
                if (auto m = ev->getIf<sf::Event::MouseButtonPressed>())
                {
                    sf::Vector2f mouse =
                        window.mapPixelToCoords(m->position, window.getDefaultView());

                    if (volumeSlider.knob.getGlobalBounds().contains(mouse))
                    {
                        volumeSlider.dragging = true;
                    }
                }

                if (ev->getIf<sf::Event::MouseButtonReleased>())
                {
                    volumeSlider.dragging = false;
                }
            }
        
        
            if (auto k = ev->getIf<sf::Event::KeyPressed>())
            {
                sf::Vector2f size_updation = mapView.getSize();
                const sf::Vector2f ORIGINAL_VIEW_SIZE(1920.f, 1080.f);
                if (k->code == sf::Keyboard::Key::I)
                {
                    if (size_updation.x > MIN_VIEW_WIDTH)
                        mapView.zoom(ZOOM_IN);
                }
                else if (k->code == sf::Keyboard::Key::O) {
                    if (size_updation.x < MAX_VIEW_WIDTH)
                        mapView.zoom(ZOOM_OUT);
                    else if (size_updation.x > MAX_VIEW_WIDTH) {
                        mapView.setSize(ORIGINAL_VIEW_SIZE);
                        mapView.setCenter(ORIGINAL_VIEW_SIZE / 2.f);
                    }
                }


                if (k->code == sf::Keyboard::Key::L &&
                    states.top() == GameState::Playing)
                    states.push(GameState::Inventory);

                if (k->code == sf::Keyboard::Key::Escape)
                {
                    if (ghost) ghost.reset();
                    else if (states.size() > 1) states.pop();
                }

                if (states.top() == GameState::Upgrade &&
                    upgradeUI.selected)
                {
                    UpgradeNode* cur = upgradeUI.selected->upgradeNode;

                    if (k->code == sf::Keyboard::Key::Num1 && cur->children.size() > 0)
                        applyUpgrade(*upgradeUI.selected, cur->children[0], coins);

                    if (k->code == sf::Keyboard::Key::Num2 && cur->children.size() > 1)
                        applyUpgrade(*upgradeUI.selected, cur->children[1], coins);

                    if (k->code == sf::Keyboard::Key::Num3 && cur->children.size() > 2)
                        applyUpgrade(*upgradeUI.selected, cur->children[2], coins);
                }

                if (k->code == sf::Keyboard::Key::P) {
                    if (states.top() == GameState::Upgrade)
                        states.pop();
                }

                if (k->code == sf::Keyboard::Key::Z) {
                    states.push(GameState::Settings);
                }

                if (k->code == sf::Keyboard::Key::M) {
                    if (states.top() == GameState::Settings)
                        states.pop();
                }
            }

            if (auto m = ev->getIf<sf::Event::MouseButtonPressed>())
            {
                sf::Vector2f ui =
                    window.mapPixelToCoords(
                        m->position, window.getDefaultView());

                if (states.top() == GameState::Inventory &&
                    slot.box.getGlobalBounds().contains(ui))
                {
                    ghost.emplace(towerTex);
                    sf::FloatRect b = ghost->getLocalBounds();
                    ghost->setOrigin(
                        sf::Vector2f{ b.size.x / 2.f, b.size.y / 2.f });
                    ghost->setColor(sf::Color(255, 255, 255, 120));
                    states.pop();
                }
                else if (ghost &&
                    m->button == sf::Mouse::Button::Left)
                {
                    towers.emplace_back(
                        towerTex,
                        ghost->getPosition(),
                        baseTowerDB[0]);
                    ghost.reset();
                }


                if (m->button == sf::Mouse::Button::Right && !ghost)
                {
                    sf::Vector2f world =
                        window.mapPixelToCoords(m->position, mapView);

                    for (auto& t : towers)
                    {
                        if (t.sprite.getGlobalBounds().contains(world))
                        {
                            upgradeUI.selected = &t;
                            if (states.top() != GameState::Upgrade)
                                states.push(GameState::Upgrade);

                            break;
                        }
                    }
                }

                if (states.top() == GameState::Upgrade &&
                    m->button == sf::Mouse::Button::Left)
                {
                    sf::Vector2f ui =
                        window.mapPixelToCoords(
                            m->position, window.getDefaultView());

                    if (upgradeUI.button.getGlobalBounds().contains(ui))
                    {
                        UpgradeNode* cur = upgradeUI.selected->upgradeNode;

                        if (!cur->children.empty())
                        {
                            applyUpgrade(
                                *upgradeUI.selected,
                                cur->children[0],
                                coins
                            );
                        }
                    }

                }

                if (states.top() == GameState::Settings)
                {
                    if (auto m = ev->getIf<sf::Event::MouseButtonPressed>())
                    {
                        sf::Vector2f pos =
                            window.mapPixelToCoords(m->position, window.getDefaultView());

                        if (backText.getGlobalBounds().contains(pos))
                        {
                            states.pop(); 
                        }
                    }
                }



            }
        }

        if (states.top() == GameState::Playing || states.top() == GameState::Inventory || states.top() == GameState::Upgrade) {
            if (ghost)
            {
                sf::Vector2f w =
                    window.mapPixelToCoords(
                        sf::Mouse::getPosition(window), mapView);

                sf::Vector2f snap(
                    std::floor(w.x / TILE_SIZE) * TILE_SIZE + TILE_SIZE / 2.f,
                    std::floor(w.y / TILE_SIZE) * TILE_SIZE + TILE_SIZE / 2.f
                );

                ghost->setPosition(snap);
                rangePreview.setRadius(baseTowerDB[0].range);
                rangePreview.setOrigin(
                    sf::Vector2f{ baseTowerDB[0].range, baseTowerDB[0].range });
                rangePreview.setPosition(snap);
            }


            if (states.top() == GameState::Upgrade &&
                upgradeUI.selected != nullptr)
            {
                UpgradeNode* cur = upgradeUI.selected->upgradeNode;

                std::string text = "UPGRADES:\n";

                for (size_t i = 0; i < cur->children.size(); ++i)
                {
                    text += std::to_string(i + 1) + ". " +
                        cur->children[i]->name +
                        " (" + std::to_string(cur->children[i]->cost) + ")\n";
                }

                if (cur->children.empty())
                    text += "\nMAX LEVEL";

                upgradeUI.text.setString(text);
            }

            


            if (waveMgr.currentWave < waveMgr.waves.size())
            {
                Wave& wave = waveMgr.waves[waveMgr.currentWave];
                if (!wave.queue.empty() &&
                    waveMgr.spawnClock.getElapsedTime().asSeconds() >
                    wave.interval)
                {
                    const Path& p = paths[waveMgr.pathCounter % paths.size()];
                    enemies.emplace_back(
                        enemyTex,
                        wave.queue.front().type,
                        &p);
                    wave.queue.pop();
                    waveMgr.pathCounter++;
                    waveMgr.spawnClock.restart();
                }

                if (wave.queue.empty() && enemies.empty())
                    waveMgr.currentWave++;
            }


            for (auto& e : enemies)
            {
                if (e.nodeIndex + 1 >= e.path->size())
                {

                    castleHP -= CASTLE_DAMAGE_PER_ENEMY;
                    e.hp = 0.f;
                    continue;
                }

                sf::Vector2f d =
                    (*e.path)[e.nodeIndex + 1].pos -
                    e.sprite.getPosition();

                float len = std::hypot(d.x, d.y);
                if (len < 4.f)
                    e.nodeIndex++;
                else
                    e.sprite.move(
                        sf::Vector2f{
                            (d.x / len) * enemyDB[e.type].speed * dt,
                            (d.y / len) * enemyDB[e.type].speed * dt
                        });
            }



            for (auto& t : towers)
            {
                if (t.fireClock.getElapsedTime().asSeconds() <
                    t.stats.fireRate)
                    continue;

                Enemy* target = nullptr;
                float best = t.stats.range;

                for (auto& e : enemies)
                {
                    float dx =
                        e.sprite.getPosition().x -
                        t.sprite.getPosition().x;
                    float dy =
                        e.sprite.getPosition().y -
                        t.sprite.getPosition().y;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist < best)
                    {
                        best = dist;
                        target = &e;
                    }
                }

                if (target)
                {
                    projectiles.emplace_back(
                        t.sprite.getPosition(),
                        target,
                        t.stats.damage);
                    t.fireClock.restart();
                }
            }





            for (auto& p : projectiles)
            {
                if (!p.target) continue;
                sf::Vector2f d =
                    p.target->sprite.getPosition() -
                    p.shape.getPosition();
                float len = std::sqrt(d.x * d.x + d.y * d.y);
                if (len < 6.f)
                {
                    p.target->hp -= p.damage;
                    p.target = nullptr;
                }
                else
                    p.shape.move(
                        sf::Vector2f{ (d.x / len) * PROJECTILE_SPEED * dt,
                                     (d.y / len) * PROJECTILE_SPEED * dt });
            }

            enemies.erase(
                std::remove_if(enemies.begin(), enemies.end(),
                    [&](Enemy& e)
                    {
                        if (e.hp <= 0.f)
                        {
                            coins += enemyDB[e.type].bounty;
                            return true;
                        }
                        return false;
                    }),
                enemies.end());

            projectiles.erase(
                std::remove_if(projectiles.begin(), projectiles.end(),
                    [](const Projectile& p)
                    { return p.target == nullptr; }),
                projectiles.end());


            window.clear();
            window.setView(mapView);

            window.draw(layer0);
            window.draw(layer1);

            for (auto& t : towers) window.draw(t.sprite);

            for (auto& e : enemies)
            {
                window.draw(e.sprite);

                float r = e.hp / enemyDB[e.type].maxHP;
                sf::RectangleShape bg(sf::Vector2f{ 26.f, 4.f });
                bg.setPosition(sf::Vector2f{
                    e.sprite.getPosition().x - 13.f,
                    e.sprite.getPosition().y - 28.f });
                bg.setFillColor(sf::Color(120, 0, 0));

                sf::RectangleShape fg(sf::Vector2f{ 26.f * r, 4.f });
                fg.setPosition(bg.getPosition());
                fg.setFillColor(sf::Color::Green);

                window.draw(bg);
                window.draw(fg);
            }

            for (auto& p : projectiles) window.draw(p.shape);

            if (ghost)
            {
                window.draw(rangePreview);
                window.draw(*ghost);
            }

            window.draw(castleHPBg);
            window.draw(castleHPFg);
            window.draw(coinText);
            window.draw(enemiesalive);
            window.draw(waveText);
            window.draw(towerCountText);
        }
        window.setView(window.getDefaultView());

        if (states.top() == GameState::Inventory)
        {
            window.draw(slot.box);
            if (slot.icon) window.draw(*slot.icon);
        }

        if (states.top() == GameState::Upgrade)
        {
            window.setView(window.getDefaultView());
            window.draw(upgradeUI.panel);
            window.draw(upgradeUI.text);
            window.draw(upgradeUI.button);
        }

        if (castleHP <= 0.f && states.top() != GameState::GameOver)
        {
            while (!states.empty()) states.pop();
            states.push(GameState::GameOver);
        }

        
        if (states.top() == GameState::Mainmenu)
        {   
            window.draw(backgroundimo);
            window.setView(window.getDefaultView());
            window.draw(titleText);
            window.draw(playText);
            window.draw(settingsText);
            window.draw(quitText);
        }

        
        if (states.top() == GameState::Settings)
        {
            window.setView(window.getDefaultView());
            window.draw(settingsTitle);
            window.draw(backText);
        }

        
        if (states.top() == GameState::GameOver)
        {
            window.setView(window.getDefaultView());
            window.draw(gameOverText);
            window.draw(restartText);
        }

        if (states.top() == GameState::Settings)
        {
            window.setView(window.getDefaultView());
            window.draw(settingsTitle);
            window.draw(backText);

        }

        if (states.top() == GameState::Settings && volumeSlider.dragging)
        {
            float mouseX =
                window.mapPixelToCoords(
                    sf::Mouse::getPosition(window),
                    window.getDefaultView()
                ).x;

            mouseX = std::clamp(mouseX,
                volumeSlider.minX,
                volumeSlider.maxX);

            volumeSlider.knob.setPosition(
                { mouseX, volumeSlider.knob.getPosition().y }
            );

            float percent =
                (mouseX - volumeSlider.minX) /
                (volumeSlider.maxX - volumeSlider.minX);

            volumeSlider.value = percent * 100.f;

            bgMusic.setVolume(volumeSlider.value);
        }

        if (states.top() == GameState::Settings)
        {
            window.draw(volumeSlider.track);
            window.draw(volumeSlider.knob);
        }



        window.display();

        if (consoleClock.getElapsedTime().asSeconds() >= 1.0f)
        {
            std::cout << "\n-----------------------------------------------------------\n";

            std::cout << "GameState Stack Size: " << states.size() << "\n";
            std::cout << "Top State: ";
            switch (states.top())
            {
            case GameState::Mainmenu:  std::cout << "Main Menu\n"; break;
            case GameState::Playing:   std::cout << "Playing\n"; break;
            case GameState::Inventory: std::cout << "Inventory\n"; break;
            case GameState::Upgrade:   std::cout << "Upgrade\n"; break;
            case GameState::GameOver:  std::cout << "Game Over\n"; break;
            }

            
            std::cout << "Wave: " << waveMgr.currentWave + 1
                << "/" << waveMgr.waves.size() << "\n";

            if (waveMgr.currentWave < waveMgr.waves.size())
            {
                std::cout << "Enemies remaining in queue (FIFO): "
                    << waveMgr.waves[waveMgr.currentWave].queue.size()
                    << "\n";
            }

            
            std::cout << "Enemies Alive (Vector): " << enemies.size() << "\n";
            std::cout << "Towers Deployed (Vector): " << towers.size() << "\n";
            std::cout << "Projectiles Active (Vector): " << projectiles.size() << "\n";

            
            std::cout << "EnemyDB Entries (unordered_map): "
                << enemyDB.size() << "\n";
            std::cout << "TowerDB Entries (unordered_map): "
                << baseTowerDB.size() << "\n";

           
            std::cout << "Upgrade Tree Root: " << upgradeRoot.name << "\n";
            std::cout << "Available Upgrades: "
                << upgradeRoot.children.size() << "\n";

            
            std::cout << "Paths Loaded (Graph): " << paths.size() << "\n";

            
            std::cout << "Coins: " << coins << "\n";
            std::cout << "Castle HP: "
                << static_cast<int>(castleHP)
                << "/" << CASTLE_MAX_HP << "\n";

            std::cout << "---------------------------------------------\n";

            consoleClock.restart();
        }
    }
    return 0;
}


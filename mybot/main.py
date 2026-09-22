import helper as unswbc
from helper import Direction, EdgeType, Position
from collections import deque
import random

ct: unswbc.Controller
game: unswbc.Game

# A shared 32-bit key to XOR against Sonar messages to prevent enemy interception
SECRET_KEY = 0b10101010101010101010101010101010 

# Global memory to track pearl spawn timers across out-of-vision tiles
known_pearl_timers: dict[tuple[int, int], int] = {}

def process_sonar() -> None:
    """Reads and decrypts all incoming sonar broadcasts from the current turn."""
    for msg in ct.get_sonar_messages():
        decrypted = msg ^ SECRET_KEY
        
        # Verify the signature to ensure it's from our team, not enemy noise
        if (decrypted & 0xFFFF) == 0xFFFF:
            x = (decrypted >> 24) & 0xFF
            y = (decrypted >> 16) & 0xFF
            ct.output_log(f"Secure transmission received: target {x}, {y}")

def send_encrypted_sonar(target_x: int, target_y: int) -> None:
    """Packs coordinates into a 32-bit integer, encrypts it, and broadcasts it."""
    signature = 0xFFFF
    message = (target_x << 24) | (target_y << 16) | signature
    
    encrypted = message ^ SECRET_KEY
    ct.send_sonar(encrypted)

def is_safe(direction: Direction) -> bool:
    """Checks the immediate adjacent edge and destination tile for fatal obstacles."""
    here = ct.get_position()
    tile = ct.get_tile(here)
    
    if tile is None: 
        return False
        
    # Check for lethal Kelp edges
    edge = tile.get_edge(direction)
    if edge.get_edge_type() == EdgeType.KELP:
        return False
        
    # Check destination tile for other dragon bodies (including self)
    ahead = ct.get_tile(here.add_dir(direction))
    if ahead is None or ahead.get_dragon() is not None:
        return False 
        
    return True

def is_enemy_threat_nearby(target_pos: Position) -> bool:
    """
    Scans the entire vision grid for enemy heads that could sprint into our target.
    Higher ID enemies have not moved yet and require a larger safety buffer.
    """
    my_team = ct.get_team()
    my_id = ct.get_id()
    width, height = game.get_map_size()
    
    for tile in ct.get_tiles():
        part = tile.get_dragon()
        
        # We only care about enemy heads
        if part is not None and part.is_head() and part.get_team() != my_team:
            enemy_pos = tile.get_position()
            enemy_id = part.get_id()
            
            # Calculate the shortest Manhattan distance, accounting for map wrap-around
            dx = min(abs(target_pos.x - enemy_pos.x), width - abs(target_pos.x - enemy_pos.x))
            dy = min(abs(target_pos.y - enemy_pos.y), height - abs(target_pos.y - enemy_pos.y))
            distance = dx + dy
            
            # If the enemy hasn't moved yet (higher ID), they could sprint into us.
            # A safety buffer of 2 tiles prevents most accidental sprint collisions.
            if enemy_id > my_id and distance <= 2:
                return True
                
            # If they have already moved (lower ID), their position is static for this round,
            # so they are only a threat if they are already standing on the exact target tile.
            if enemy_id < my_id and distance == 0:
                return True
                
    return False

# ====================== PEARL TARGETING =========================

def find_best_pearl_target() -> tuple[list[Direction], int]:
    """
    Uses BFS to find reachable pearls, heavily prioritizing actual existing pearls 
    over future timer spawns to prevent camping.
    """
    start_pos = ct.get_position()
    queue = deque([(start_pos, [])])
    visited = {start_pos}
    targets = []
    
    while queue:
        current_pos, path = queue.popleft()
        
        if len(path) > 6:
            continue
            
        current_tile = ct.get_tile(current_pos)
        if current_tile is None:
            continue
            
        pearl_time = current_tile.get_pearl_time()
        has_pearl = current_tile.has_pearl()
        
        # We focus primarily on actual pearls, or very immediate spawns (timer == 0)
        if len(path) > 0 and (has_pearl or pearl_time == 0):
            # Existing pearls get priority 0, timer 0 gets priority 1
            priority = 0 if has_pearl else 1
            targets.append({
                "path": path,
                "priority": priority,
                "yield": 1
            })
            
        for direction in Direction.get_direction_list():
            edge = current_tile.get_edge(direction)
            if edge.get_edge_type() == EdgeType.KELP:
                continue
                
            next_pos = current_pos.add_dir(direction)
            
            if next_pos not in visited:
                next_tile = ct.get_tile(next_pos)
                
                if next_tile is not None and next_tile.get_dragon() is None:
                    if not is_enemy_threat_nearby(next_pos):
                        visited.add(next_pos)
                        queue.append((next_pos, path + [direction]))
                    
    if not targets:
        return [], 0
        
    # Sort by priority (real pearls first) then shortest path
    targets.sort(key=lambda t: (t["priority"], len(t["path"])))
    best_target = targets[0]
    return best_target["path"], best_target["yield"]


def update_pearl_memory() -> None:
    """Scans current vision, remembers spawns, and clears targets if they were eaten."""
    current_round = game.get_round_num()
    
    for tile in ct.get_tiles():
        pos = tile.get_position()
        p_time = tile.get_pearl_time()
        pos_tuple = (pos.x, pos.y)
        
        if tile.has_pearl():
            known_pearl_timers[pos_tuple] = current_round - 1
        elif p_time == 0:
            known_pearl_timers[pos_tuple] = current_round
        elif p_time > 0:
            known_pearl_timers[pos_tuple] = current_round + p_time
        
        # If we are standing on or looking at a known target tile and it has no pearl and no countdown, clear it!
        if pos_tuple in known_pearl_timers and not tile.has_pearl() and p_time < 0:
            del known_pearl_timers[pos_tuple]


def get_best_global_target() -> tuple[int, int] | None:
    """Finds the best global pearl, ignoring future timers to prevent circling empty tiles."""
    if not known_pearl_timers:
        return None
        
    current_round = game.get_round_num()
    here = ct.get_position()
    width, height = game.get_map_size()
    
    best_target = None
    best_score = float('inf')
    
    # Clean up expired entries first
    expired = [pos for pos, spawn_round in known_pearl_timers.items() if spawn_round < current_round - 5]
    for pos in expired:
        del known_pearl_timers[pos]
        
    for (px, py), spawn_round in known_pearl_timers.items():
        # Calculate wrapped Manhattan distance
        dx = min(abs(here.x - px), width - abs(here.x - px))
        dy = min(abs(here.y - py), height - abs(here.y - py))
        distance = dx + dy
        
        time_to_spawn = spawn_round - current_round
        
        # Ignore pearls that are far in the future; only target things ready now or in 1-2 rounds
        if time_to_spawn > 2:
            continue
            
        score = (time_to_spawn * 5) + distance
        
        if score < best_score:
            best_score = score
            best_target = (px, py)
            
    return best_target

def move_towards_target(target_x: int, target_y: int) -> bool:
    """Attempts to move one step closer to a global target coordinate."""
    here = ct.get_position()
    width, height = game.get_map_size()
    
    # Determine the optimal directions to close the distance considering wrap-around
    desired_dirs = []
    
    dx = target_x - here.x
    wrapped_dx = width - abs(dx)
    if dx != 0:
        if (dx > 0 and abs(dx) <= wrapped_dx) or (dx < 0 and abs(dx) > wrapped_dx):
            desired_dirs.append(Direction.EAST)
        else:
            desired_dirs.append(Direction.WEST)
            
    dy = target_y - here.y
    wrapped_dy = height - abs(dy)
    if dy != 0:
        if (dy > 0 and abs(dy) <= wrapped_dy) or (dy < 0 and abs(dy) > wrapped_dy):
            desired_dirs.append(Direction.SOUTH)
        else:
            desired_dirs.append(Direction.NORTH)
            
    # Try the most direct routes first
    for direction in desired_dirs:
        if is_safe(direction):
            ct.output_log(f"Routing toward known pearl at {target_x}, {target_y}")
            ct.make_move(direction)
            return True
            
    return False

# ====================== MAIN TURN EXECUTION =========================

def execute_turn() -> None:
    """The main decision-making sequence executed by every living dragon, every turn."""
    # 1. Update Global Memory
    update_pearl_memory()
    
    # 2. Process Intelligence
    process_sonar()
    
    # 3. Macro Economy Expansion (Phase-Shifted)
    # We only allow splitting in the first 100 rounds to build our swarm.
    if game.get_round_num() < 100:
        if ct.can_split(3): 
            ct.output_log("Splitting to expand economy")
            ct.do_split(3)
            return

    # 4. Local Pearl Hunting (Single-Step BFS)
    # This reintroduces the search for pearls up to 6 steps away, but only executes a single safe step.
    target_path, expected_yield = find_best_pearl_target()
    if len(target_path) > 0:
        ct.output_log("Marching toward nearby pearl")
        ct.make_move(target_path[0])
        return

    # 5. Global Navigation Fallback (Out-of-Vision Pearls)
    best_global = get_best_global_target()
    if best_global is not None:
        if move_towards_target(best_global[0], best_global[1]):
            return
            
    # 6. Momentum-Based Exploration (Prevents Circling)
    here = ct.get_position()
    safe_dirs = []
    
    for direction in Direction.get_direction_list():
        if is_safe(direction):
            safe_dirs.append(direction)
            
    if safe_dirs:
        current_facing = ct.get_dir()
        # We filter out the exact opposite direction to prevent the dragon from pacing back and forth
        forward_dirs = [d for d in safe_dirs if d != current_facing.get_opposite()]
        
        if forward_dirs:
            ct.output_log("Exploring forward")
            ct.make_move(random.choice(forward_dirs))
        else:
            ct.output_log("Dead end reached, turning around")
            ct.make_move(random.choice(safe_dirs))
        return

    # 7. Emergency Escape Split
    # If safe_dirs is completely empty, the head is trapped. We split to let the tail escape.
    if ct.can_split(2):
        ct.output_log("Head trapped! Emergency split to reverse direction.")
        # Splitting a size of 2 drops the tail as a new dragon, keeping the parent alive for one more turn
        ct.do_split(2)
        return

    # 8. Absolute Last Resort
    # If we cannot move or split, we try to output any valid move to avoid a crash penalty
    for direction in Direction.get_direction_list():
        if ct.get_tile(here).get_edge(direction).get_edge_type() != EdgeType.KELP:
            ct.make_move(direction)
            return
            
    ct.make_move(Direction.NORTH)

def main() -> None:
    """Initializes the game state and loops through turns until elimination."""
    global ct, game
    # Seed so we get the same random generator every time.
    random.seed(0)
    
    ct, game = unswbc.init()

    while unswbc.update(ct, game):
        execute_turn()
        unswbc.end_turn()

if __name__ == "__main__":
    main()
#!/bin/bash

botA="mybot_cpp_arch4/"
botB="mybot_cpp_arch4.2/"

seeds=(3001 3002 3003 3004 3005 3006 3007 3008 3009 3010 3011 3012 3013 3014 3015 3016 3017 3018 3019 3020)

botA_wins=0
botB_wins=0
games=0

maps=(
    "maps/queen_of_spades.map"
)

for map in "${maps[@]}"; do
    echo "Processing $map"

    for seed in "${seeds[@]}"; do

        # Run both A/B arrangements
        for pair in "$botA $botB" "$botB $botA"; do

            read -r teamA teamB <<< "$pair"

            result=$(unswbc run \
                --sandbox \
                --no-replay \
                --seed "$seed" \
                "$map" \
                "$teamA" \
                "$teamB" \
                2>&1)

            games=$((games + 1))

            # Team A = first bot
            # Team B = second bot
            if [[ $result =~ team[[:space:]]+([AB])[[:space:]]+wins[[:space:]]+after ]]; then

                winning_team="${BASH_REMATCH[1]}"

                if [[ "$winning_team" == "A" ]]; then
                    winning_bot="$teamA"
                else
                    winning_bot="$teamB"
                fi

                echo "  seed $seed ($teamA vs $teamB): team $winning_team wins [$winning_bot]"

                if [[ "$winning_bot" == "$botA" ]]; then
                    botA_wins=$((botA_wins + 1))
                else
                    botB_wins=$((botB_wins + 1))
                fi

            else
                echo "  seed $seed ($teamA vs $teamB): no winner line found"
                echo "$result" | tail -n 5
            fi

        done
    done
done

echo
echo "Games run: $games"
echo "$botA wins: $botA_wins"
echo "$botB wins: $botB_wins"